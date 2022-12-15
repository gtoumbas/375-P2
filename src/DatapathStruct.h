#pragma once
#include "UtilityStruct.h"
#include "UtilityEnum.h"
#include "DriverFunctions.h"
#include "Cache.h"
//Note that an instruction that modifies the PC will never throw an
//exception or be prone to errors from the memory abstraction.
//Thus a single value is enough to depict the status of an instruction.
#define NOINC_PC 1
#define OVERFLOW 2
#define ILLEGAL_INST 3
#define NUM_REGS 32

// Static global variables...
// static uint32_t regs[NUM_REGS];

// HAZARD + FORWARD UNITS AND STATE
// *------------------------------------------------------------*
struct FORWARD_UNIT; // do not delete or compilation error
struct BRANCH_FORWARD_UNIT;
struct HAZARD_UNIT;  // do not delete or compilation error
struct EXECUTOR;  // do not delete or compilation error

struct STATE
{
    uint32_t pc, branch_pc;
    uint32_t cycles, wait_cycles;
    uint32_t regs[NUM_REGS];
    IF_ID_STAGE if_id_stage;
    ID_EX_STAGE id_ex_stage;
    EX_MEM_STAGE ex_mem_stage;
    MEM_WB_STAGE mem_wb_stage;
    PipeState pipe_state;
    FORWARD_UNIT* fwd;
    BRANCH_FORWARD_UNIT* branch_fwd;  
    HAZARD_UNIT*  hzd;
    EXECUTOR* exec;
    Cache *i_cache, *d_cache;
    bool stall;
    bool exception;
    bool finish; 
};


struct FORWARD_UNIT 
{
    HAZARD_TYPE fwd1 = HAZARD_TYPE::NONE;
    HAZARD_TYPE fwd2 = HAZARD_TYPE::NONE;
    HAZARD_TYPE fwdWriteStore = HAZARD_TYPE::NONE;
    uint32_t mem_value, wb_value;

    void checkFwd(STATE& state) 
    {
        fwd1 = fwd2 = fwdWriteStore = HAZARD_TYPE::NONE;
        checkMEM(state);
        checkWB(state);
        checkWriteStore(state);
    }
private:

    void checkMEM(STATE& state) // example: add $t1, ... -> add $t2, $t1, $t0 -> forward from ex_mem to id_ex 
    {
        uint32_t where = (state.ex_mem_stage.ctrl.regDst) ? state.ex_mem_stage.decodedInst.rd : state.ex_mem_stage.decodedInst.rt;
        if (!state.ex_mem_stage.ctrl.regWrite || where == 0) {
            return;
        }
        fwd1 = (where == state.id_ex_stage.decodedInst.rs) ? (HAZARD_TYPE::MEM_HAZ) : (HAZARD_TYPE::NONE);
        fwd2 = (where == state.id_ex_stage.decodedInst.rt) ? (HAZARD_TYPE::MEM_HAZ) : (HAZARD_TYPE::NONE);
    }
    
    void checkWB(STATE& state) // example: add $t1, ... -> nop -> add $t2, $t1, $t0 -> forward from mem_wb to id_ex 
    {
        uint32_t where = (state.mem_wb_stage.ctrl.regDst) ? state.mem_wb_stage.decodedInst.rd : state.mem_wb_stage.decodedInst.rt;
        if (!state.mem_wb_stage.ctrl.regWrite || where == 0) {
            return;
        }
        // check that there is no such op between this and in id_ex
        fwd1 = ((where == state.id_ex_stage.decodedInst.rs) && (fwd1 == HAZARD_TYPE::NONE)) ? HAZARD_TYPE::WB_HAZ : fwd1;
        fwd2 = ((where == state.id_ex_stage.decodedInst.rt) && (fwd2 == HAZARD_TYPE::NONE)) ? HAZARD_TYPE::WB_HAZ : fwd2;
    }


    void checkWriteStore(STATE& state) { // example: lw $t0, addr -> sw $t0, addr -> forward from mem/wb to ex/mem 
        uint32_t where = (state.mem_wb_stage.ctrl.regDst) ? state.mem_wb_stage.decodedInst.rd : state.mem_wb_stage.decodedInst.rt;
        if (!state.ex_mem_stage.ctrl.memWrite || !state.mem_wb_stage.ctrl.regWrite || where == 0) {
            fwdWriteStore = HAZARD_TYPE::NONE;
            return;
        }
        fwdWriteStore = (where == state.ex_mem_stage.decodedInst.rt) ? HAZARD_TYPE::WRITE_STORE_HAZ : HAZARD_TYPE::NONE;
    }

};


struct BRANCH_FORWARD_UNIT {
    HAZARD_TYPE fwd1 = HAZARD_TYPE::NONE;
    HAZARD_TYPE fwd2 = HAZARD_TYPE::NONE;
    uint32_t mem_value, wb_value;

    void checkFwd(STATE& state) 
    {
        DecodedInst di;
        decodeInst(state.if_id_stage.instr, di);
        fwd1 = fwd2 = HAZARD_TYPE::NONE;
        if (di.op != OP_BEQ && di.op != OP_BNE){
            return;
        }
        checkEX(state, di);
        checkMEM(state, di);
        checkLOADMEM(state, di);
        checkWB(state, di);
    }

private:
    void checkEX(STATE& state, DecodedInst& decodedInst) // example: add $t1, ... -> beq $t1, $t0 -> stall and then forward from ex/mem to if/id
    {
        uint32_t where = (state.id_ex_stage.ctrl.regDst) ? state.id_ex_stage.decodedInst.rd : state.id_ex_stage.decodedInst.rt;
        if (!state.id_ex_stage.ctrl.regWrite || where == 0) {
            return;
        }
        fwd1 = (where == decodedInst.rs) ? HAZARD_TYPE::BRANCH_EX_HAZ : HAZARD_TYPE::NONE;
        fwd2 = (where == decodedInst.rt) ? HAZARD_TYPE::BRANCH_EX_HAZ : HAZARD_TYPE::NONE;  
    }

    void checkLOADMEM(STATE& state, DecodedInst& decodedInst) // example: load $t1, addr -> nop ->  beq $t1, $t0 -> stalls and then forward from ex/mem to if/id
    {   
        uint32_t where = (state.ex_mem_stage.ctrl.regDst) ? state.ex_mem_stage.decodedInst.rd : state.ex_mem_stage.decodedInst.rt;
        if (!state.ex_mem_stage.ctrl.memRead || !state.ex_mem_stage.ctrl.regWrite || where == 0) {
            return;
        }
        // check that there is no such op between this and branch
        fwd1 = ((where == decodedInst.rs) && (fwd1 == HAZARD_TYPE::NONE)) ? HAZARD_TYPE::BRANCH_LOAD_MEM_HAZ : fwd1;
        fwd2 = ((where == decodedInst.rt) && (fwd2 == HAZARD_TYPE::NONE)) ? HAZARD_TYPE::BRANCH_LOAD_MEM_HAZ : fwd2;   
    }

    void checkMEM(STATE &state, DecodedInst& decodedInst) { // example: add $t1, ... -> nop -> beq $t1, $t0 -> forward form ex/mem to if/id
        uint32_t where = (state.ex_mem_stage.ctrl.regDst) ? state.ex_mem_stage.decodedInst.rd : state.ex_mem_stage.decodedInst.rt;
        if (state.ex_mem_stage.ctrl.memRead || !state.ex_mem_stage.ctrl.regWrite || where == 0) {
            return;
        }
        // check that there is no such op between this and branch
        fwd1 = ((where == decodedInst.rs) && (fwd1 == HAZARD_TYPE::NONE)) ? HAZARD_TYPE::BRANCH_MEM_HAZ : fwd1;
        fwd2 = ((where == decodedInst.rt) && (fwd2 == HAZARD_TYPE::NONE)) ? HAZARD_TYPE::BRANCH_MEM_HAZ : fwd2;   
      
    }
    void checkWB(STATE &state, DecodedInst& decodedInst) { // example: add $t1, ... -> nop -> beq $t1, $t0 -> forward form ex/mem to if/id
        uint32_t where = (state.mem_wb_stage.ctrl.regDst) ? state.mem_wb_stage.decodedInst.rd : state.mem_wb_stage.decodedInst.rt;
        if (!state.ex_mem_stage.ctrl.regWrite || where == 0) {
            return;
        }
        // check that there is no such op between this and branch
        fwd1 = ((where == decodedInst.rs) && (fwd1 == HAZARD_TYPE::NONE) && (fwd1 == HAZARD_TYPE::NONE)) ? HAZARD_TYPE::BRANCH_WB_HAZ : fwd1;
        fwd2 = ((where == decodedInst.rt) && (fwd2 == HAZARD_TYPE::NONE) && (fwd2 == HAZARD_TYPE::NONE)) ? HAZARD_TYPE::BRANCH_WB_HAZ : fwd2;   
      
    }
};


struct HAZARD_UNIT 
{
    bool jump;

    void checkHazard(STATE& state, DecodedInst& decodedInst) {
        state.stall = false;
        if (state.id_ex_stage.ctrl.memRead) {    // check load_use if op = MemRead
            checkLoadUse(state);
        } else {                // check jump or branch: op = J, JAL, BEQ, BNE
            checkBranch(state, decodedInst);
            std::cout << "EXITING checkHazard()\n";
        }
    }


private:
    void checkLoadUse(STATE& state) 
    {
        uint32_t if_id_reg1 = instructBits(state.if_id_stage.instr, 25, 21);
        uint32_t if_id_reg2 = instructBits(state.if_id_stage.instr, 20, 16);
        if (state.id_ex_stage.ctrl.memRead && ((state.id_ex_stage.decodedInst.rt == if_id_reg1) ||
            (state.id_ex_stage.decodedInst.rt == if_id_reg2))) {
                state.stall = true;
        } else {
            state.stall = false;
        }
    }

    // done during the id stage
    void checkBranch(STATE& state, DecodedInst& decodedInst) 
    {
        uint32_t readReg1 = state.regs[decodedInst.rs];
        uint32_t readReg2 = state.regs[decodedInst.rt];
        uint32_t old_pc = state.if_id_stage.npc;
        uint32_t op = decodedInst.op;

        if (!JB_OP.count(op)) {
            return;
        }

        // do forwarding if op = BEQ or BNE
        if (op == OP_BNE || op == OP_BEQ) {
            switch (state.branch_fwd -> fwd1) {
                case BRANCH_EX_HAZ:         // cant be load, because loaduse would handle this. 1 stall and fwd
                    std::cout << "BRANCH_EX_HAZ 1\n";
                    state.stall = true;
                    return;
                case BRANCH_MEM_HAZ:        // no stalls, fwd
                    readReg1 = state.branch_fwd -> mem_value;
                    std::cout << "BRANCH_MEM_HAZ 1: " << readReg1 <<'\n';
                    break;
                case BRANCH_LOAD_MEM_HAZ:   // 1 stall and fwd
                    state.stall = true;
                    return;
                case BRANCH_WB_HAZ:         // no stall, fwd 
                    readReg1 = state.branch_fwd -> wb_value;
                    break;
                default:
                    readReg1 = state.regs[decodedInst.rs];
            }
            // do forwarding
            switch (state.branch_fwd -> fwd2) {
                case BRANCH_EX_HAZ:
                    std::cout << "BRANCH_EX_HAZ 1\n";
                    state.stall = true;
                    return;
                case BRANCH_MEM_HAZ:
                    std::cout << "BRANCH_MEM_HAZ 2: " << readReg2 << '\n';
                    readReg2 = state.branch_fwd -> mem_value;
                    break;
                case BRANCH_LOAD_MEM_HAZ:
                    state.stall = true;
                    return;
                case BRANCH_WB_HAZ:
                    readReg2 = state.branch_fwd -> wb_value;
                    break;
                default:
                    readReg2 = state.regs[decodedInst.rt];
            }
        }

        std::cout << "COMPARE VALUES ARE " << readReg1 << ' ' <<  readReg2 << '\n';

        switch (op) {
            case OP_BEQ:
                jump = (readReg1 == readReg2);
                state.branch_pc = old_pc + (decodedInst.signExtIm << 2);
                return;
            case OP_BNE:
                jump = (readReg1 != readReg2);
                state.branch_pc = old_pc + (decodedInst.signExtIm << 2);
                return;
            case OP_JAL:
                // state.regs[REG_RA] = old_pc + 4; // not +8 because PC is incremented in the IF()
                // fall through
            case OP_J:
                jump = true;
                state.branch_pc = (old_pc & 0xf0000000) | (decodedInst.addr); 
                return;
        }
    }
};

struct EXECUTOR 
{
    uint8_t getSign(uint32_t value)
    {
        return (value >> 31) & 0x1;
    }

    int doAddSub(uint32_t& aluResult, uint32_t s1, uint32_t s2, bool isAdd, bool checkOverflow)
    {
        bool overflow = false;
        uint32_t result = 0;

        // Not sure why she was casting this before
        if (isAdd){result = s1 + s2;}
        else{result = s1 - s2;}

        if (checkOverflow)
        {
                if (isAdd)
                {
                    overflow = getSign(s1) == getSign(s2) && getSign(s2) != getSign(result);
                }
                else
                {
                    overflow = getSign(s1) != getSign(s2) && getSign(s2) == getSign(result);
                }
        }

        if (overflow)
        {
                // Inform the caller that overflow occurred so it can take appropriate action.
                return OVERFLOW;
        }

        // Otherwise update state and return success.
        aluResult = result;

        return 0;
    }

    void executeR(STATE& state){

        // Do I need to do anything with control signals here?
        
        uint32_t funct = state.id_ex_stage.decodedInst.funct;
        uint32_t rd = state.id_ex_stage.decodedInst.rd; // register number
        uint32_t rs = state.id_ex_stage.decodedInst.rs; // register number
        uint32_t rt = state.id_ex_stage.decodedInst.rt; // register number

        uint32_t rs_value = state.id_ex_stage.readData1;    // Amir
        uint32_t rt_value = state.id_ex_stage.readData2;    // Amir

        uint32_t shamt = state.id_ex_stage.decodedInst.shamt;
        // Perform ALU operation and store in EX/MEM aluResult
        uint32_t aluResult;
        int ret = 0;
        switch (funct){
            case FUN_ADD:
                ret = doAddSub(aluResult, rs_value, rt_value, true, true);
                break;
            case FUN_ADDU:
                ret = doAddSub(aluResult, rs_value, rt_value, true, false);
                break;
            case FUN_AND:
                aluResult = rs_value & rt_value;
                break;
            case FUN_JR:
                //FIXME Should we be updating the PC here?
                state.ex_mem_stage.npc = rs_value;
                break;
            case FUN_NOR:
                aluResult = ~(rs_value | rt_value);
                break;
            case FUN_OR:
                aluResult = rs_value | rt_value;
                break;
            case FUN_SLT:
                if ((rs_value >> 31) != (rt_value >> 31)) { // Different signs
                    aluResult = (rs_value >> 31) ? 1 : 0; 
                } else {
                    aluResult = (rs_value < rt_value) ? 1 : 0;
                }
                break;
            case FUN_SLTU:
                aluResult = (rs_value < rt_value) ? 1 : 0;
                break;
            case FUN_SLL:
                aluResult = rt_value << shamt;
                break;
            case FUN_SRL:
                aluResult = rt_value >> shamt;
                break;
            case FUN_SUB:
                ret = doAddSub(aluResult, rs_value, rt_value, false, true);
                break;
            case FUN_SUBU:
                ret = doAddSub(aluResult, rs_value, rt_value, false, false);
                break;
                // TODO jump to exception address=
        }
        if (ret == OVERFLOW) {
            state.exception = true;
            return;
        }
        state.ex_mem_stage.aluResult = aluResult;
    }

    void executeI(STATE& state) {
        uint32_t op = state.id_ex_stage.decodedInst.op;
        uint32_t rt = state.id_ex_stage.decodedInst.rt; // register number
        uint32_t rs = state.id_ex_stage.decodedInst.rs; // register number

        uint32_t rs_value = state.id_ex_stage.readData1;    // Amir
        uint32_t rt_value = state.id_ex_stage.readData2;    // Amir

        uint32_t imm = state.id_ex_stage.decodedInst.imm;
        uint32_t seImm = state.id_ex_stage.decodedInst.signExtIm;
        uint32_t zeImm = imm;
        uint32_t aluResult;
        uint32_t addr = rs_value + seImm;

        int ret = 0;
        uint32_t oldPC = state.id_ex_stage.npc;

        switch(op){
            case OP_ADDI:
                ret = doAddSub(aluResult, rs_value, seImm, true, true);
                break;
            case OP_ADDIU:
                ret = doAddSub(aluResult, rs_value, seImm, true, false);
                break;
            case OP_ANDI:
                aluResult = rs_value & zeImm;
                break;
            case OP_BEQ:
                /*if (rs == rt){
                    state.ex_mem_stage.npc = 4 + (seImm << 2);
                }*/
                break;
            case OP_BNE:
                /*if (rs != rt){
                    state.ex_mem_stage.npc = 4 + (seImm << 2);
                }*/
                break;
            case OP_LBU:
                aluResult = addr;
                break;
            case OP_LHU:
                // Double check
                aluResult = addr;
                break;
            case OP_LUI:
                aluResult = (imm << 16);
                break;
            case OP_LW:
                aluResult = addr;
                break;
            case OP_ORI:
                aluResult = rs_value | zeImm;
                break;
            case OP_SLTI:
                if ((rs_value >> 31) != (seImm >> 31)) { // Different signs
                    aluResult = (rs_value >> 31) ? 1 : 0; 
                } else {
                    aluResult = (rs_value < seImm) ? 1 : 0;
                }
                break;
            case OP_SLTIU:
                aluResult = (rs_value < seImm) ? 1 : 0;
                break;
            case OP_SB:
                aluResult = addr;
                break;
            case OP_SW:
                aluResult = addr;
                break;
            case OP_SH:
                aluResult = addr;
                break;
        }

        if (ret == OVERFLOW) {
            state.exception = true;
            return;
        }

        state.ex_mem_stage.aluResult = aluResult;
    }

};





// UTILITY FUNCTIONS
