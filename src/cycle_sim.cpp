#include <iostream>
#include <iomanip>
#include <fstream>
#include <string.h>
#include <errno.h>
#include "MemoryStore.h"
#include "RegisterInfo.h"
#include "EndianHelpers.h"
#include "DatapathStruct.h"
#include<set>

#define END 0xfeedfeed
#define EXCEPTION_ADDR 0x8000

static MemoryStore* mem;

extern void dumpRegisterStateInternal(RegisterInfo & reg, std::ostream & reg_out);

// Print State
void printState(STATE & state, std::ostream & out, bool printReg)
{
    out << "State at beginning of cycle " << state.cycles << ":" << std::endl;
    out << "PC: " << std::hex << state.pc << std::endl;
    out << "IF/ID: " << std::hex << state.if_id_stage.instr << std::endl;
    out << "ID/EX: " << std::hex << state.id_ex_stage.decodedInst.instr << std::endl;
    out << "EX/MEM: " << std::hex << state.ex_mem_stage.decodedInst.instr << std::endl;
    out << "MEM/WB: " << std::hex << state.mem_wb_stage.decodedInst.instr << std::endl;
   // out << "WB: " << std::hex << state.wb_stage.decodedInst.instr << std::endl;

    if(printReg){
        out << "Registers:" << std::endl;
        for(int i = 0; i < NUM_REGS; i++){
            out << "$" << std::dec << i << ": " << std::hex << regs[i] << std::endl;
        }
    }
    out << std::endl;
}


// *------------------------------------------------------------*
// Control 
// *------------------------------------------------------------*


void updateControl(STATE & state, DecodedInst & decIns, CONTROL& ctrl){

    if (VALID_OP.count(decIns.op) == 0) { // ILLEGAL_INST
        state.exception = true;
        return;
    }

    if (decIns.op == OP_ZERO) {
        ctrl = CONTROL_RTYPE;
        return;
    }
    if (decIns.op == OP_LW || decIns.op == OP_LHU || decIns.op == OP_LBU || decIns.op == OP_LUI) {
        ctrl = CONTROL_LOAD;
        return;
    }
    if (decIns.op == OP_SW || decIns.op == OP_SH || decIns.op == OP_SB || decIns.op == OP_SLTI || decIns.op == OP_SLTIU) {
        ctrl = CONTROL_STORE;
        return;
    }
    
    ctrl = CONTROL_NOP;
}

// Mem helper
int doLoad(uint32_t addr, MemEntrySize size, uint8_t rt)
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
            regs[rt] = value & 0xFF;
            break;
    case HALF_SIZE:
            regs[rt] = value & 0xFFFF;
            break;
    case WORD_SIZE:
            regs[rt] = value;
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

    // mux. if control_hazard.branch asserted, then jump to new address
    if (state.hzd -> jump) {
        state.pc = state.branch_pc;
    } 

    // fetch instruction
    mem->getMemValue(state.pc, instr, WORD_SIZE);
    
    // Update state
    state.if_id_stage.instr = instr;
    state.if_id_stage.npc = state.pc + 4;

    // increment pc
    state.pc += 4;
}


void ID(STATE& state){
    
    // Read instruction from IF stage and Decode
    uint32_t instr = state.if_id_stage.instr;
    DecodedInst decodedInst;
    CONTROL ctrl;

    decodeInst(instr, decodedInst);
    updateControl(state, decodedInst, ctrl);

    if (state.exception) {
        state.branch_pc = EXCEPTION_ADDR;
        return;
    }

    state.hzd -> jump = false;  // erase previously written value 
    state.hzd -> checkHazard(state, decodedInst);

    uint32_t op = decodedInst.op;
    if(op == OP_BNE || op == OP_BEQ || op == OP_J || op == OP_JAL){ // set inst to zero, because branch or jump is completed
        state.id_ex_stage = ID_EX_STAGE{};
        return;
    }

    state.id_ex_stage.decodedInst = decodedInst;
    state.id_ex_stage.npc = state.if_id_stage.npc;
    state.id_ex_stage.readData1 = regs[decodedInst.rs];
    state.id_ex_stage.readData2 = regs[decodedInst.rt];
    state.id_ex_stage.ctrl = ctrl;
}

void EX(STATE & state)
{
    state.fwd->checkFwd(state);
    uint32_t readData1, readData2; 
    EXECUTOR executor;

    // set readReg1
    switch (state.fwd -> forward1) {
        case HAZARD_TYPE::EX:
            readData1 = state.ex_mem_stage.aluResult;
            break;
        
        case HAZARD_TYPE::MEM:
            readData1 = state.mem_wb_stage.aluResult;
            break;

        case HAZARD_TYPE::NONE:
            readData1 = state.id_ex_stage.readData1;
    }
    // set readReg2
    switch (state.fwd -> forward2) {
        case HAZARD_TYPE::EX:
            readData2 =  state.ex_mem_stage.aluResult;
            break;
        
        case HAZARD_TYPE::MEM:
            readData2 = state.mem_wb_stage.aluResult;
            break;

        case HAZARD_TYPE::NONE:
            readData2 = state.id_ex_stage.readData2;
    }

    // OVERWRITE REGISTER VALUES IF VALUES FORWARDED (different from what actually happens)
    state.id_ex_stage.readData1 = readData1;
    state.id_ex_stage.readData2 = readData2;

    // DO ALU Operations
    uint32_t op = state.id_ex_stage.decodedInst.op;
    if (op == OP_ZERO) {
        executor.executeR(state);
    } else if (I_TYPE.count(op) != 0) {
        executor.executeI(state);
    } // branch and jump finished by this time

    if (state.exception) {
        state.branch_pc = EXCEPTION_ADDR; // the textbook says we should add 4 and substract 4 in the exception handler
        state.ex_mem_stage = EX_MEM_STAGE{};
        state.id_ex_stage = ID_EX_STAGE{};
        state.if_id_stage = IF_ID_STAGE{};
        return;
    } 
    state.ex_mem_stage.decodedInst = state.id_ex_stage.decodedInst;
    state.ex_mem_stage.npc = state.id_ex_stage.npc;
    state.ex_mem_stage.memoryAddr = state.id_ex_stage.readData2;
    state.ex_mem_stage.ctrl = state.id_ex_stage.ctrl;

}


void MEM(STATE & state){

    uint32_t op = state.ex_mem_stage.decodedInst.op;
    uint32_t rt = state.ex_mem_stage.decodedInst.rt;
    uint32_t addr = state.ex_mem_stage.aluResult;
    uint32_t imm = state.ex_mem_stage.decodedInst.imm;
    uint32_t data;
    
    int ret = 0;
    switch(op){
        // Storing
        case OP_SW:
            ret = mem->setMemValue(addr, rt, WORD_SIZE);
            break;
        case OP_SH:
            ret = mem->setMemValue(addr, rt, HALF_SIZE);
            break;
        case OP_SB:
            ret = mem->setMemValue(addr, rt, BYTE_SIZE);
            break;
        // Loading
        case OP_LW:
            ret = doLoad(addr, WORD_SIZE, data);
            break;
        case OP_LHU:
            ret = doLoad(addr, HALF_SIZE, data);
            break;
        case OP_LBU:
            ret = doLoad(addr, BYTE_SIZE, data);
            break;
        // Default
        default:
            data = state.ex_mem_stage.aluResult;
    }
    
    state.mem_wb_stage.decodedInst = state.ex_mem_stage.decodedInst;
    state.mem_wb_stage.aluResult = state.ex_mem_stage.aluResult;
    state.mem_wb_stage.data = state.ex_mem_stage.aluResult;
    state.mem_wb_stage.ctrl = state.ex_mem_stage.ctrl;
}


void WB(STATE & state){
    uint32_t writeData = (state.mem_wb_stage.ctrl.memToReg) ? state.mem_wb_stage.data : state.mem_wb_stage.aluResult;
    uint32_t where = (state.mem_wb_stage.ctrl.regDst) ? state.mem_wb_stage.decodedInst.rd : state.mem_wb_stage.decodedInst.rt;
  
    if (state.mem_wb_stage.ctrl.regWrite) {
        regs[where] = writeData;
    } 
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
    
    for(int i = 0; i < 32; i++){ regs[i] = 0;}

    std::ifstream prog; 
    prog.open(argv[1], std::ios::binary | std::ios::in);
    mem = createMemoryStore();

    if (initMemory(prog))
    {
        return -EBADF;
    }
    
    while (true)
    {
        printState(state, std::cout, false);
        
        
        WB(state);
        
        
        MEM(state);
        
        
        EX(state);
        // Arithmetic overflow
        if (state.exception) {
            state.pc = state.branch_pc;
            state.exception = false;
        }

        ID(state);

        // Illegal Instruction
        if (state.exception) {
            state.pc = state.branch_pc;
            state.exception = false;
        }

        
        if (!state.stall) {
            IF(state);
        }
                

        state.cycles++;
        if(state.pc > 16) break;
    }


    printState(state, std::cout, true);
    dumpMemoryState(mem);
    
}





