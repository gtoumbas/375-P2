#include <iostream>
#include <iomanip>
#include <fstream>
#include <string.h>
#include <errno.h>
#include "RegisterInfo.h"
#include "EndianHelpers.h"
#include "DatapathStruct.h"
#include "Cache.h"
#include<set>

#define END 0xfeedfeed
#define EXCEPTION_ADDR 0x8000

static MemoryStore* mem;

extern void dumpRegisterStateInternal(RegisterInfo & reg, std::ostream & reg_out);

// Print State
void printState(STATE & state, std::ostream & out, bool printReg)
{
    out << "\nState at beginning of cycle " << state.cycles << ":" << std::endl;
    out << "PC: " << std::hex << state.pc << std::endl;
    out << "IF/ID: " << std::hex << state.if_id_stage.instr << std::endl;
    out << "ID/EX: " << std::hex << state.id_ex_stage.decodedInst.instr << std::endl;
    out << "EX/MEM: " << std::hex << state.ex_mem_stage.decodedInst.instr << std::endl;
    out << "MEM/WB: " << std::hex << state.mem_wb_stage.decodedInst.instr << std::endl;
   // out << "WB: " << std::hex << state.wb_stage.decodedInst.instr << std::endl;

    if(printReg){
        out << "Registers:" << std::endl;
        for(int i = 0; i < NUM_REGS; i++){
            out << "$" << std::dec << i << ": " << std::hex << state.regs[i] << std::endl;
        }
    }
}


// *------------------------------------------------------------*
// Control 
// *------------------------------------------------------------*


void updateControl(STATE & state, DecodedInst & decIns, CONTROL& ctrl){
    // End of program check
    if (decIns.instr == 0xfeedfeed) {
        ctrl = CONTROL_NOP;
        return;
    }

    if (VALID_OP.count(decIns.op) == 0) { // ILLEGAL_INST
        state.exception = true;
        return;
    }

    if (decIns.op == OP_ZERO) {
        ctrl = CONTROL_RTYPE;
        return;
    }
    if (LOAD_OP.count(decIns.op) > 0) {
        ctrl = CONTROL_LOAD;
        return;
    }
    if (STORE_OP.count(decIns.op) > 0) {
        ctrl = CONTROL_STORE;
        return;
    }
    if (I_TYPE_NO_LS.count(decIns.op) > 0) {
        ctrl = CONTROL_ITYPE;
        return;
    }
    ctrl = CONTROL_NOP;
}

// Mem helper
int doLoad(STATE &state, uint32_t addr, MemEntrySize size, uint32_t& data)
{
    uint32_t value = 0;
    int ret = 0;
    ret = mem->getMemValue(addr, value, size);
    if (ret)
    {
            std::cout << "Could not get mem value" << std::endl;
            return ret;
    }

    switch (size)
    {
    case BYTE_SIZE:
            data = value & 0xFF;
            break;
    case HALF_SIZE:
            data = value & 0xFFFF;
            break;
    case WORD_SIZE:
            data = value;
            break;
    default:
            std::cerr << "Invalid size passed, cannot read/write memory" << std::endl;
            return -EINVAL;
    }

    return 0;
}

// *------------------------------------------------------------*
// Function for each stage
// *------------------------------------------------------------*
void IF(STATE & state){
    uint32_t instr = 0;
    
    // fetch instruction if 0xfeedfeed has not been reached 
    if (!state.finish){
        mem->getMemValue(state.pc, instr, WORD_SIZE);
    }
    
    // Update state
    state.if_id_stage.instr = instr;
    state.if_id_stage.npc = state.pc + 4;

    // increment pc

    state.pc = (state.hzd -> jump) ? state.branch_pc : state.pc + 4;
}


void ID(STATE& state){
    
    // Read instruction from IF stage and Decode
    uint32_t instr = state.if_id_stage.instr;
    if (instr == 0xfeedfeed) {
        state.finish = true;
        // state.id_ex_stage = ID_EX_STAGE{};
        // return;
    }
    DecodedInst decodedInst;
    CONTROL ctrl;

    decodeInst(instr, decodedInst);
    updateControl(state, decodedInst, ctrl);

    if (state.exception) {
        state.branch_pc = EXCEPTION_ADDR;
        // Don't need to squash because not truly parallel
        return;
    }
    if (!state.finish) {
        state.hzd -> jump = false;  // erase previously written value 
        state.hzd -> checkHazard(state, decodedInst);
    }
    // if state.stall = true: this might be because 1) load_use   2) if not load_use, then it is branch register forwarding issue
    // have to flush id_ex_stage and not execute IF() in this iteration (the pc will stay the same in the next iteration)
    // also if op was branch or jump do not push it forward (exception: JAL)
    if (state.stall || (JB_OP.count(decodedInst.op) > 0 && decodedInst.op != OP_JAL)){
        state.id_ex_stage = ID_EX_STAGE{};
        std::cout << "STALLING IN ID()\n";
        return;
    }

    state.id_ex_stage.decodedInst = decodedInst;
    state.id_ex_stage.npc = state.if_id_stage.npc;
    state.id_ex_stage.readData1 = state.regs[decodedInst.rs];
    state.id_ex_stage.readData2 = state.regs[decodedInst.rt];
    state.id_ex_stage.ctrl = ctrl;

}

void EX(STATE & state)
{
    uint32_t readData1, readData2; 
    EXECUTOR executor;

    // set readReg1
    switch (state.fwd -> fwd1) {
        case HAZARD_TYPE::MEM_HAZ:  // example: add $t1, ... (in MEM now)-> add $t0, $t1, $t1 (in EX now): 
            state.id_ex_stage.readData1 = state.fwd->mem_value;
            break;
        
        case HAZARD_TYPE::WB_HAZ: // example: add $t1, ... (in WB now)-> nop -> add $t0, $t1, $t1 (in EX now): 
            state.id_ex_stage.readData1 = state.fwd->wb_value;
            break;

        case HAZARD_TYPE::NONE:
            break;
    }
    // set readReg2
    switch (state.fwd -> fwd2) {
        case HAZARD_TYPE::MEM_HAZ:
            state.id_ex_stage.readData2 =  state.fwd->mem_value;
            break;
        
        case HAZARD_TYPE::WB_HAZ:
            state.id_ex_stage.readData2 = state.fwd->wb_value;
            break;

        case HAZARD_TYPE::NONE:
            if (state.id_ex_stage.ctrl.regDst) {
                state.id_ex_stage.readData2 = state.id_ex_stage.readData2;
            } else {
                state.id_ex_stage.readData2 = state.id_ex_stage.decodedInst.signExtIm;
            }
    }

    uint32_t op = state.id_ex_stage.decodedInst.op;

    // DO ALU Operations
    if (state.id_ex_stage.decodedInst.instr != 0xfeedfeed) {
        if (op == OP_ZERO) {
            executor.executeR(state);
        } else if (I_TYPE_NO_LS.count(op) != 0 || LOAD_OP.count(op) != 0 || STORE_OP.count(op) != 0) {
            executor.executeI(state);
        } // branch and jump finished by this time
    }

    if (state.exception) {
        state.branch_pc = EXCEPTION_ADDR; 
        state.ex_mem_stage = EX_MEM_STAGE{};
        state.id_ex_stage = ID_EX_STAGE{};
        state.if_id_stage = IF_ID_STAGE{};
        return;
    } 
    state.ex_mem_stage.decodedInst = state.id_ex_stage.decodedInst;
    state.ex_mem_stage.npc = state.id_ex_stage.npc;
    state.ex_mem_stage.ctrl = state.id_ex_stage.ctrl;
    state.ex_mem_stage.setMemValue = state.id_ex_stage.readData2;
}


void MEM(STATE & state){ 

    // forward values
    state.fwd->mem_value = state.ex_mem_stage.aluResult;
    state.branch_fwd->mem_value = state.ex_mem_stage.aluResult;

    uint32_t op = state.ex_mem_stage.decodedInst.op;
    uint32_t rt = state.ex_mem_stage.decodedInst.rt;
    uint32_t addr = state.ex_mem_stage.aluResult;
    uint32_t imm = state.ex_mem_stage.decodedInst.imm;
    uint32_t setValue;
    uint32_t data;
    int ret = 0;

    // sw $t0, addr: t0 may be forwarded by the instruction in WB
    switch (state.fwd -> fwdWriteStore) {
        case HAZARD_TYPE::WRITE_STORE_HAZ:
            setValue = state.fwd -> wb_value;
            break;
        case HAZARD_TYPE::NONE:
            setValue = state.ex_mem_stage.setMemValue;
    }
    
    if (state.ex_mem_stage.decodedInst.instr != 0xfeedfeed) {
        switch(op){
            // Storing
            case OP_SW:
                ret = mem->setMemValue(addr, setValue, WORD_SIZE); 
                break;
            case OP_SH:
                ret = mem->setMemValue(addr, instructBits(setValue, 31, 16), HALF_SIZE); 
                break;
            case OP_SB:
                ret = mem->setMemValue(addr, instructBits(setValue, 31, 24), BYTE_SIZE);
                break;
            // Loading
            case OP_LW:
                ret = doLoad(state, addr, WORD_SIZE, data);
                break;
            case OP_LHU:
                ret = doLoad(state, addr, HALF_SIZE, data);
                break;
            case OP_LBU:
                ret = doLoad(state, addr, BYTE_SIZE, data);
                break;
            // Default
            default:
                data = state.ex_mem_stage.aluResult;
        }
    }
    
    state.mem_wb_stage.decodedInst = state.ex_mem_stage.decodedInst;
    state.mem_wb_stage.aluResult = state.ex_mem_stage.aluResult;
    state.mem_wb_stage.data = data;
    state.mem_wb_stage.ctrl = state.ex_mem_stage.ctrl;
    state.mem_wb_stage.npc = state.ex_mem_stage.npc;
}


void WB(STATE & state){
    // Check for 0xfeefeed
    if (state.mem_wb_stage.decodedInst.instr == 0xfeedfeed) {
        return;
    }

    uint32_t writeData = (state.mem_wb_stage.ctrl.memToReg) ? state.mem_wb_stage.data : state.mem_wb_stage.aluResult;
    uint32_t where = (state.mem_wb_stage.ctrl.regDst) ? state.mem_wb_stage.decodedInst.rd : state.mem_wb_stage.decodedInst.rt;
    uint32_t op = state.mem_wb_stage.decodedInst.op;
    if (state.mem_wb_stage.ctrl.regWrite) {
        state.regs[where] = writeData;
        // forward values 
        state.fwd->wb_value = writeData;        
        state.branch_fwd->wb_value = writeData; 
    }

    // JAL write reg
    if (op == OP_JAL){
        state.regs[REG_RA] = state.mem_wb_stage.npc + 4;
    }

    state.mem_wb_stage.data = writeData; 
}



// Driver stuff
int initMemory(std::ifstream & inputProg)
{
    // Check if mem is already initialized

    if (inputProg && mem)
    {
        uint32_t curVal = 0;
        uint32_t addr = 0;

        while (inputProg.read((char *)(&curVal), sizeof(uint32_t)))
        {
            curVal = ConvertWordToBigEndian(curVal);
            int ret = mem->setMemValue(addr, curVal, WORD_SIZE);

            if (ret)
            {
                std::cout << "Could not set memory value!" << std::endl;
                return -EINVAL;
            }

            // We're reading 4 bytes each time...
            addr += 4;
        }
    }
    else
    {
        std::cout << "Invalid file stream or memory image passed, could not initialise memory values" << std::endl;
        return -EINVAL;
    }

    return 0;
}

// Main function
int main(int argc, char *argv[])
{
    STATE state = {};
    state.exec = new EXECUTOR{};
    state.hzd = new HAZARD_UNIT{};
    state.fwd = new FORWARD_UNIT{};
    state.branch_fwd = new BRANCH_FORWARD_UNIT{};
    
    for(int i = 0; i < 32; i++){ state.regs[i] = 0;}

    std::ifstream prog; 
    prog.open(argv[1], std::ios::binary | std::ios::in);
    mem = createMemoryStore();

    if (initMemory(prog))
    {
        return -EBADF;
    }
    
    uint32_t DrainIters = 3;
    while (DrainIters--)
    {
        printState(state, std::cout, false); 
        state.cycles++;
        // forwarding units
        state.fwd->checkFwd(state);
        state.branch_fwd->checkFwd(state);

        WB(state);
        

        MEM(state);
        
        
        EX(state);
        // Arithmetic overflow
        if (state.exception) {
            std::cout << "EXCEPTION\n";
            state.pc = state.branch_pc;
            state.exception = false;
        }

        ID(state);

        // Illegal Instruction
        if (state.exception) {
            std::cout << "EXCEPTION\n";
            state.pc = state.branch_pc;
            state.exception = false;
        }


        if (state.finish) {
            IF(state);
            continue;
        }


        ++DrainIters;
        if (state.stall) { 
           continue; 
        }
        IF(state);
    }


    printState(state, std::cout, true);
    dumpMemoryState(mem);
    
}





