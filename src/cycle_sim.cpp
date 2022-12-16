#include <iostream>
#include <iomanip>
#include <fstream>
#include <string.h>
#include <errno.h>
#include <algorithm>
#include "RegisterInfo.h"
#include "EndianHelpers.h"
#include "DatapathStruct.h"
#include<set>

#define END 0xfeedfeed
#define EXCEPTION_ADDR 0x8000

static MemoryStore* mem;

// Global State variable
STATE state;


extern void dumpRegisterStateInternal(RegisterInfo & reg, std::ostream & reg_out);

// Print State
void printState(std::ostream & out, bool printReg)
{
    out << "\nState at beginning of cycle " << state.sim_stats.totalCycles << ":" << std::endl;
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


void updateControl(DecodedInst & decIns, CONTROL& ctrl){
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
int doLoad(STATE& state, uint32_t addr, MemEntrySize size, uint32_t& data)
{
    uint32_t value = 0;
    int ret = 0;
    ret = state.d_cache->getCacheValue(addr, value, size);

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

    return ret;
}

// *------------------------------------------------------------*
// Function for each stage
// *------------------------------------------------------------*
void IF(){
    uint32_t instr = 0;
    state.pipe_state.ifInstr = instr;

    if (state.stall){
        return;
    }
    
    // decrement wait_cycles and return if still have to wait
    if ((state.if_wait_cycles = std::max(state.if_wait_cycles - 1, 0)) > 0) {   // still blocked
        return;
    }
    
    // fetch instruction if 0xfeedfeed has not been reached 
    if (!state.end_at_id){
        auto hit = state.i_cache->getCacheValue(state.pc, instr, WORD_SIZE);
        if (hit != CACHE_RET::HIT && (state.i_cache -> Penalty() > 0)) {  // miss -> set penalty cycles
            state.if_wait_cycles = state.i_cache -> Penalty() - 1;
            return;
        }
    }
    // update pipe state
    state.pipe_state.ifInstr = instr;
  
    if (!state.if_id_stage.block) {         // if next stage not blocked, then move forward
        state.if_id_stage.instr = instr;
        state.if_id_stage.npc = state.pc + 4;
        state.pc = (state.hzd -> jump || state.exception) ? state.branch_pc : state.pc + 4;
    }
    
}


void ID(){
    uint32_t instr = state.if_id_stage.instr;

    // Update pipestate
    state.pipe_state.idInstr = instr;

    if (instr == 0xfeedfeed) {
        state.end_at_id = true;
    }
    DecodedInst decodedInst;
    CONTROL ctrl;

    decodeInst(instr, decodedInst);
    updateControl(decodedInst, ctrl);

    if (state.exception) {
        state.branch_pc = EXCEPTION_ADDR;
        return;
    }
    if (!state.end_at_id) {
        state.hzd -> jump = false;  // erase previously written value 
        state.hzd -> checkHazard(state, decodedInst);
    }


    if (state.stall || (JB_OP.count(decodedInst.op) > 0 && decodedInst.op != OP_JAL)){
        state.id_ex_stage = ID_EX_STAGE{};
        return;
    }

    if (!state.id_ex_stage.block){
        state.id_ex_stage.decodedInst = decodedInst;
        state.id_ex_stage.npc = state.if_id_stage.npc;
        state.id_ex_stage.readData1 = state.regs[decodedInst.rs];
        state.id_ex_stage.readData2 = state.regs[decodedInst.rt];
        state.id_ex_stage.ctrl = ctrl;
        state.if_id_stage = IF_ID_STAGE{};
    }
}

void EX()
{
    uint32_t readData1, readData2; 

    // Update pipestate
    state.pipe_state.exInstr = state.id_ex_stage.decodedInst.instr;

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
            break;
    }

    uint32_t op = state.id_ex_stage.decodedInst.op;

    // DO ALU Operations
    if (state.id_ex_stage.decodedInst.instr != 0xfeedfeed) {
        if (op == OP_ZERO) {
            state.exec -> executeR(state);
        } else if (I_TYPE_NO_LS.count(op) != 0 || LOAD_OP.count(op) != 0  || STORE_OP.count(op) != 0) {
            state.exec -> executeI(state);
        } // branch and jump finished by this time
    }

    if (state.exception) {                  // flush everything
        state.branch_pc = EXCEPTION_ADDR; 
        state.ex_mem_stage = EX_MEM_STAGE{};
        state.id_ex_stage = ID_EX_STAGE{};
        state.if_id_stage = IF_ID_STAGE{};
        return;
    } 
    if (!state.ex_mem_stage.block){
        state.ex_mem_stage.decodedInst = state.id_ex_stage.decodedInst;
        state.ex_mem_stage.npc = state.id_ex_stage.npc;
        state.ex_mem_stage.ctrl = state.id_ex_stage.ctrl;
        state.ex_mem_stage.setMemValue = state.id_ex_stage.readData2;
        state.id_ex_stage = ID_EX_STAGE{};
    }
}


void MEM(){ 

    // forward values
    state.fwd->mem_value = state.ex_mem_stage.aluResult;
    state.branch_fwd->mem_value = state.ex_mem_stage.aluResult;
    // Update pipestate
    state.pipe_state.memInstr = state.ex_mem_stage.decodedInst.instr;

    // decrement wait_cycles and check if have to wait more
    if ((state.mem_wait_cycles = std::max(state.mem_wait_cycles - 1, 0)) > 0){ // still blocked
        return;
    }

    state.if_id_stage.block = false;
    state.id_ex_stage.block = false;
    state.ex_mem_stage.block = false;
    

    uint32_t op = state.ex_mem_stage.decodedInst.op;
    uint32_t rt = state.ex_mem_stage.decodedInst.rt;
    uint32_t addr = state.ex_mem_stage.aluResult;
    uint32_t imm = state.ex_mem_stage.decodedInst.imm;
    uint32_t setValue;
    uint32_t data;
    int ret = CACHE_RET::HIT;



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
                ret = state.d_cache->setCacheValue(addr, setValue, WORD_SIZE); 
                break;
            case OP_SH:
                ret = state.d_cache->setCacheValue(addr, instructBits(setValue, 31, 16), HALF_SIZE); 
                break;
            case OP_SB:
                ret = state.d_cache->setCacheValue(addr, instructBits(setValue, 31, 24), BYTE_SIZE);
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

    if (ret != CACHE_RET::HIT && (state.d_cache -> Penalty() > 0)) {    // if miss -> block all storages before this one
        state.mem_wait_cycles = state.d_cache->Penalty() - 1;
        state.ex_mem_stage.block = true;
        state.id_ex_stage.block = true;
        state.if_id_stage.block = true;
        return;
    }
    
    state.mem_wb_stage.decodedInst = state.ex_mem_stage.decodedInst;
    state.mem_wb_stage.aluResult = state.ex_mem_stage.aluResult;
    state.mem_wb_stage.data = data;
    state.mem_wb_stage.ctrl = state.ex_mem_stage.ctrl;
    state.mem_wb_stage.npc = state.ex_mem_stage.npc;

    state.ex_mem_stage = EX_MEM_STAGE{};
}


void WB(){
    // Update pipestate
    state.pipe_state.wbInstr = state.mem_wb_stage.decodedInst.instr;

    // Check for 0xfeefeed
    if (state.mem_wb_stage.decodedInst.instr == 0xfeedfeed) {
        state.pipe_state.wbInstr = 0;
        state.end_program = true;
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

}



int initSimulator(CacheConfig &icConfig, CacheConfig &dcConfig, MemoryStore *mainMem){
    // Init simulator 
    state = {};
    state.exec = new EXECUTOR{};
    state.hzd = new HAZARD_UNIT{};
    state.fwd = new FORWARD_UNIT{};
    state.branch_fwd = new BRANCH_FORWARD_UNIT{};
    mem = mainMem;

    // Init Cache
    state.i_cache = new Cache(icConfig, mainMem);
    state.d_cache = new Cache(dcConfig, mainMem);

    // Set regs to zero 
    for(int i = 0; i < 32; i++){ state.regs[i] = 0;}

    return 1;
}

// int runCycles(uint32_t cycles){
//     uint32_t startingCycle = state.sim_stats.totalCycles;
//     uint32_t DrainIters = 5; //FIXME 3 or 4?
//     bool finEarly = false;
//     while (DrainIters--){
//         state.fwd->checkFwd(state);
//         state.branch_fwd->checkFwd(state);

//         WB();
//         MEM();
//         EX();
//         ID();
//         if (state.end_program) {
//             IF();
//             finEarly = true;
//             continue;
//         }

//         ++DrainIters;
//         if (state.stall){
//             state.pipe_state.ifInstr = 0;
//             continue;
//         }
//         IF();
//     }
//     printState(std::cout, true);

//     dumpMemoryState(mem);
//     state.pipe_state.cycle = state.sim_stats.totalCycles;
//     dumpPipeState(state.pipe_state);

//     if (finEarly) {
//         return 1;
//     }
//     return 0;
// }
int runCycles(uint32_t cycles){
    uint32_t startingCycle = state.sim_stats.totalCycles;
    bool finEarly = false;
    while (state.sim_stats.totalCycles < (cycles + startingCycle)){
        state.fwd->checkFwd(state);
        state.branch_fwd->checkFwd(state);

        WB();
        MEM();
        EX();
        ID();
        IF();
        state.sim_stats.totalCycles++;
        if(state.end_program){
            finEarly = true;
            break;
        }
    }
    printState(std::cout, true);

    dumpMemoryState(mem);
    state.pipe_state.cycle = state.sim_stats.totalCycles - 1;
    dumpPipeState(state.pipe_state);

    if (finEarly) {
        return 1;
    }
    return 0;
}


int runTillHalt(){
    // uint32_t DrainIters = 5; {
    while(!state.end_program){
        // printState(std::cout, false);
        state.fwd->checkFwd(state);
        state.branch_fwd->checkFwd(state);
        WB();
        MEM();
        EX();
        ID();
        IF();
        state.sim_stats.totalCycles++;
    } 

    printState(std::cout, true);

    dumpMemoryState(mem);
    state.pipe_state.cycle = state.sim_stats.totalCycles-1;
    dumpPipeState(state.pipe_state);

}


int finalizeSimulator(){
    // Write back dirty cache values, does not need to be cycle accurate
    state.d_cache->drain();
    state.sim_stats.icHits = state.i_cache->Hits();
    state.sim_stats.icMisses = state.i_cache->Misses();
    state.sim_stats.dcHits = state.d_cache->Hits();
    state.sim_stats.dcMisses = state.d_cache->Misses();


    //Dump RegisterInfo
    RegisterInfo regInfo = {};
    regInfo.at = state.regs[REG_AT];
    for (int i = 0; i < V_REG_SIZE; i++){
        regInfo.v[i] = state.regs[REG_V0 + i];
    }
    for (int i = 0; i < A_REG_SIZE; i++){
        regInfo.a[i] = state.regs[REG_A0 + i];
    }
    for (int i = 0; i < T_REG_SIZE-2; i++){
        regInfo.t[i] = state.regs[REG_T0 + i];
    }
    for (int i = 0; i < S_REG_SIZE; i++){
        regInfo.s[i] = state.regs[REG_S0 + i];
    }
    regInfo.t[8]= state.regs[REG_T8];
    regInfo.t[9] = state.regs[REG_T9];
    for (int i = 0; i < K_REG_SIZE; i++){
        regInfo.k[i] = state.regs[REG_K0 + i];
    }
    regInfo.gp = state.regs[REG_GP];
    regInfo.sp = state.regs[REG_SP];
    regInfo.fp = state.regs[REG_FP];
    regInfo.ra = state.regs[REG_RA];

    printSimStats(state.sim_stats);
    dumpRegisterState(regInfo);
    dumpMemoryState(mem);

    return 0;

}
