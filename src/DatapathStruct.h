#include "UtilityStruct.h"
#include "UtilityEnum.h"


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
struct HAZARD_UNIT;  // do not delete or compilation error
struct EXECUTOR;  // do not delete or compilation error

struct STATE
{
    uint32_t pc, branch_pc;
    uint32_t cycles;
    uint32_t regs[NUM_REGS];
    IF_ID_STAGE if_id_stage;
    ID_EX_STAGE id_ex_stage;
    EX_MEM_STAGE ex_mem_stage;
    MEM_WB_STAGE mem_wb_stage;
    FORWARD_UNIT* fwd;  
    HAZARD_UNIT*  hzd;
    EXECUTOR* exec;
    bool stall;
    bool delay;
    bool exception;
    bool finish; 
};


struct FORWARD_UNIT 
{
    HAZARD_TYPE forward1 = HAZARD_TYPE::NONE;
    HAZARD_TYPE forward2 = HAZARD_TYPE::NONE;
    uint32_t ex_value;
    uint32_t mem_value;

    void checkFwd(STATE& state) 
    {
        // forward1
        if (checkEX1(state)) {
            forward1 = HAZARD_TYPE::EX_HAZ;
            std::cout << "FORWARD EX 1\n";
        } else if (checkMEM1(state)) {
            forward1 = HAZARD_TYPE::MEM_HAZ;
            std::cout << "FORWARD_MEM 1\n";
        } else {
            forward1 = HAZARD_TYPE::NONE;
        }

        // forward2
        if (checkEX2(state)) {
            forward2 = HAZARD_TYPE::EX_HAZ;
            std::cout << "FORWARD_EX 2\n";
        } else if (checkMEM2(state)) {
            forward2 = HAZARD_TYPE::MEM_HAZ;
            std::cout << "FORWARD MEM 2\n";
        } else {
            forward2 = HAZARD_TYPE::NONE;
        } 
    }
private:

    bool checkEX1(STATE& state) 
    {
        uint32_t where = (state.ex_mem_stage.ctrl.regDst) ? state.ex_mem_stage.decodedInst.rd : state.ex_mem_stage.decodedInst.rt;
        return state.ex_mem_stage.ctrl.regWrite && (where != 0) && (where == state.id_ex_stage.decodedInst.rs);
    }

    bool checkEX2(STATE& state) 
    {

        uint32_t where = (state.ex_mem_stage.ctrl.regDst) ? state.ex_mem_stage.decodedInst.rd : state.ex_mem_stage.decodedInst.rt;
        return state.ex_mem_stage.ctrl.regWrite && (where != 0) && (where == state.id_ex_stage.decodedInst.rt);
    }


    bool checkMEM1(STATE& state) 
    {

        uint32_t where = (state.mem_wb_stage.ctrl.regDst) ? state.mem_wb_stage.decodedInst.rd : state.mem_wb_stage.decodedInst.rt;
        return state.mem_wb_stage.ctrl.regWrite && (where != 0) && (where == state.id_ex_stage.decodedInst.rs);
    }

    bool checkMEM2(STATE& state) 
    {
        uint32_t where = (state.mem_wb_stage.ctrl.regDst) ? state.mem_wb_stage.decodedInst.rd : state.mem_wb_stage.decodedInst.rt;
        return state.mem_wb_stage.ctrl.regWrite && (where != 0) && (where == state.id_ex_stage.decodedInst.rt); 
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
                std::cout << "LOAD USE\n";
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
        // done in the ID stage
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
                state.regs[REG_RA] = old_pc + 4; // not +8 because PC is incremented in the IF()
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
        std::cout << "rs_value, seImm, addr: " << rs_value << " " << seImm << " " << addr << "\n";

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
