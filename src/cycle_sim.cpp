#include <iostream>
#include <iomanip>
#include <fstream>
#include <string.h>
#include <errno.h>
#include "MemoryStore.h"
#include "RegisterInfo.h"
#include "EndianHelpers.h"

#define END 0xfeedfeed
#define EXCEPTION_ADDR 0x8000

//Note that an instruction that modifies the PC will never throw an
//exception or be prone to errors from the memory abstraction.
//Thus a single value is enough to depict the status of an instruction.
#define NOINC_PC 1
#define OVERFLOW 2
#define ILLEGAL_INST 3
#define NUM_REGS 32

// TODO 
// ID Stage Exceptions (Amir) && Initialize Control unit in ID
// EX Stage Exceptions (George)
// Execute instructions function (Later)
// Execute (R, I, J) Functions
// I, R Instructions (George)
// J Instruction function (Amir)
// Last: Cache 

extern void dumpRegisterStateInternal(RegisterInfo & reg, std::ostream & reg_out);

enum REG_IDS
{
    REG_ZERO,
    REG_AT,
    REG_V0,
    REG_V1,
    REG_A0,
    REG_A1,
    REG_A2,
    REG_A3,
    REG_T0,
    REG_T1,
    REG_T2,
    REG_T3,
    REG_T4,
    REG_T5,
    REG_T6,
    REG_T7,
    REG_S0,
    REG_S1,
    REG_S2,
    REG_S3,
    REG_S4,
    REG_S5,
    REG_S6,
    REG_S7,
    REG_T8,
    REG_T9,
    REG_K0,
    REG_K1,
    REG_GP,
    REG_SP,
    REG_FP,
    REG_RA,
};

enum OP_IDS{
    //R-type opcodes...
    OP_ZERO = 0,
    //I-type opcodes...
    OP_ADDI = 0x8,
    OP_ADDIU = 0x9,
    OP_ANDI = 0xc,
    OP_BEQ = 0x4,
    OP_BNE = 0x5,
    OP_LBU = 0x24,
    OP_LHU = 0x25,
    OP_LL = 0x30,
    OP_LUI = 0xf,
    OP_LW = 0x23,
    OP_ORI = 0xd,
    OP_SLTI = 0xa,
    OP_SLTIU = 0xb,
    OP_SB = 0x28,
    OP_SC = 0x38,
    OP_SH = 0x29,
    OP_SW = 0x2b,
    //J-type opcodes...
    OP_J = 0x2,
    OP_JAL = 0x3
};

enum FUN_IDS{
    FUN_ADD = 0x20,
    FUN_ADDU = 0x21,
    FUN_AND = 0x24,
    FUN_JR = 0x08,
    FUN_NOR = 0x27,
    FUN_OR = 0x25,
    FUN_SLT = 0x2a,
    FUN_SLTU = 0x2b,
    FUN_SLL = 0x00,
    FUN_SRL = 0x02,
    FUN_SUB = 0x22,
    FUN_SUBU = 0x23
};

using namespace std;

// Static global variables...
static uint32_t regs[NUM_REGS];
static MemoryStore *mem;



// Decoded instruction struct
struct DecodedInst{ 
    uint32_t instr;
    uint32_t op;
    uint32_t rs;
    uint32_t rt;
    uint32_t rd;
    uint32_t shamt;
    uint32_t funct;
    uint32_t imm;
    uint32_t signExtIm;
    uint32_t zeroExtImm;
    uint32_t addr;
};


// Pipeline stages 

struct IF_ID_STAGE{
    uint32_t instr;
    uint32_t npc;
};

struct ID_EX_STAGE{
    DecodedInst decodedInst;
    uint32_t npc;
    uint32_t readData1;
    uint32_t readData2;

    bool regDst;
    bool regWrite;
    bool ALUOp1;
    bool ALUOp2;
    bool ALUSrc;
    bool memWrite;
    bool memRead;
    bool branch;
    bool memToReg;
};    

struct EX_MEM_STAGE{
    DecodedInst decodedInst;
    uint32_t npc; 
    uint32_t memoryAddr; // for Load and Store -- the value of second register in the prev stage. == readData2
    uint32_t aluResult;

    // Control 
    bool regDst;
    bool regWrite;
    bool ALUOp1;
    bool ALUOp2;
    bool ALUSrc;
};

struct MEM_WB_STAGE{
    DecodedInst decodedInst;
    uint32_t aluResult;
    uint32_t data; 

    // Control
    bool regDst;
    bool regWrite;
    bool memRead;
    bool memWrite;
    bool branch;
};

// Instruction Helpers (Decoding)
// *------------------------------------------------------------*
// extract specific bits [start, end] from an instruction
uint instructBits(uint32_t instruct, int start, int end)
{
    int run = start - end + 1;
    uint32_t mask = (1 << run) - 1;
    uint32_t clipped = instruct >> end;
    return clipped & mask;
}

// sign extend while keeping values as uint's
uint32_t signExt(uint16_t smol)
{
    uint32_t x = smol;
    uint32_t extension = 0xffff0000;
    return (smol & 0x8000) ? x ^ extension : x;
}

// Function to decode instruction
void decodeInst(uint32_t inst, DecodedInst & decodedInst){
        decodedInst.instr = inst;
        decodedInst.op = instructBits(inst, 31, 26);
        decodedInst.rs = instructBits(inst, 25, 21);
        decodedInst.rt = instructBits(inst, 20, 16);
        decodedInst.rd = instructBits(inst, 15, 11);
        decodedInst.shamt = instructBits(inst, 10, 6);
        decodedInst.funct = instructBits(inst, 5, 0);
        decodedInst.imm = instructBits(inst, 15, 0);
        decodedInst.signExtIm = signExt(decodedInst.imm);
        decodedInst.zeroExtImm = decodedInst.imm;
        decodedInst.addr = instructBits(inst, 25, 0) << 2;
}


// HAZARD + FORWARD UNITS AND STATE
// *------------------------------------------------------------*
struct FORWARD_UNIT; // do not delete or compilation error
struct HAZARD_UNIT;  // do not delete or compilation error
struct EXECUTOR;  // do not delete or compilation error
struct STATE
{
    uint32_t pc, branch_pc;
    uint32_t cycles;
    IF_ID_STAGE if_id_stage;
    ID_EX_STAGE id_ex_stage;
    EX_MEM_STAGE ex_mem_stage;
    MEM_WB_STAGE mem_wb_stage;
    // added by Amir
    FORWARD_UNIT* fwd;  // do not change, it must be pointer
    HAZARD_UNIT*  hzd;
    EXECUTOR* exec;
    bool stall;
    bool delay;
};

// HAZARD DETECTION AND FORWARDING
enum class HAZARD_TYPE{
    NONE, EX, MEM
};

struct FORWARD_UNIT 
{
    HAZARD_TYPE forward1;
    HAZARD_TYPE forward2;

    void checkFwd(STATE& state) 
    {
        // forward1
        if (checkEX1(state)) {
            forward1 = HAZARD_TYPE::EX;
        } else if (checkMEM1(state)) {
            forward1 = HAZARD_TYPE::MEM;
        } else {
            forward1 = HAZARD_TYPE::NONE;
        }

        // forward2
        if (checkEX2(state)) {
            forward2 = HAZARD_TYPE::EX;
        } else if (checkMEM2(state)) {
            forward2 = HAZARD_TYPE::MEM;
        } else {
            forward2 = HAZARD_TYPE::NONE;
        } 
    }
private:

    bool checkEX1(STATE& state) 
    {
        return state.ex_mem_stage.regWrite && (state.ex_mem_stage.decodedInst.rd != 0) &&
            (state.ex_mem_stage.decodedInst.rd == state.id_ex_stage.decodedInst.rs);
    }

    bool checkEX2(STATE& state) 
    {
        return state.ex_mem_stage.regWrite && (state.ex_mem_stage.decodedInst.rd != 0) &&
            (state.ex_mem_stage.decodedInst.rd == state.id_ex_stage.decodedInst.rt);
    }


    bool checkMEM1(STATE& state) 
    {
        return state.mem_wb_stage.regWrite && (state.mem_wb_stage.decodedInst.rd != 0) &&
            (state.mem_wb_stage.decodedInst.rd == state.id_ex_stage.decodedInst.rs);
    }

    bool checkMEM2(STATE& state) 
    {
        return state.mem_wb_stage.regWrite && (state.mem_wb_stage.decodedInst.rd != 0) &&
            (state.mem_wb_stage.decodedInst.rd == state.id_ex_stage.decodedInst.rt); 
    }
};


struct HAZARD_UNIT 
{
    bool jump;

    void checkHazard(STATE& state, DecodedInst& decodedInst) {
        state.stall = false;
        if (state.id_ex_stage.memRead) {    // check load_use if op = MemRead
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
        if (state.id_ex_stage.memRead && ((state.id_ex_stage.decodedInst.rt == if_id_reg1) ||
            (state.id_ex_stage.decodedInst.rt == if_id_reg2))) {
                state.stall = true;
        } else {
            state.stall = false;
        }
    }

    // done during the id stage
    void checkBranch(STATE& state, DecodedInst& decodedInst) 
    {
        uint32_t readReg1 = regs[decodedInst.rs];
        uint32_t readReg2 = regs[decodedInst.rt];
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
                regs[REG_RA] = old_pc + 4; // not +8 because PC is incremented in the IF()
                // fall through
            case OP_J:
                jump = true;
                state.branch_pc = (old_pc & 0xf0000000) | (decodedInst.addr << 2); 
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
            default:
                // TODO jump to exception address=
                std::cerr << "Invalid funct" << std::endl;
                exit(1);
        }
        if (ret == OVERFLOW) {
            // TODO jump to exception address
            std::cerr << "Overflow" << std::endl;
            exit(1);
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
        uint32_t addr = rs + seImm;

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
                aluResult = imm << 16;
                break;
            case OP_LW:
                aluResult = addr;
                break;
            case OP_ORI:
                aluResult = rs_value | zeImm;
                break;
            case OP_SLTI:
                if (rs_value >> 31 != seImm >> 31) { // Different signs
                    aluResult = (rs_value >> 31) ? 1 : 0; 
                } else {
                    aluResult = (rs_value < seImm) ? 1 : 0;
                }
                break;
            case OP_SLTIU:
                aluResult = (rs_value < seImm) ? 1 : 0;
                break;
            // TODO Rest of store instructions
            case OP_SB:
                aluResult = addr;
                break;
            
            default:
                // TODO jump to exception address
                std::cerr << "Invalid op" << std::endl;
                exit(1);
        }

        if (ret == OVERFLOW) {
            // TODO jump to exception address
            std::cerr << "Overflow" << std::endl;
            exit(1);
        }

        state.ex_mem_stage.aluResult = aluResult;
    }

    void executeJ(STATE& state){
    }

};


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

void updateControl(STATE & state, DecodedInst & decIns){
    // Switch statement on op type
    switch(decIns.op){
        case OP_ZERO: // R type
            state.id_ex_stage.regDst = true;
            state.id_ex_stage.ALUOp1 = true;
            state.id_ex_stage.ALUOp2 = false;
            state.id_ex_stage.ALUSrc = false;
            state.id_ex_stage.branch = false;
            state.id_ex_stage.memRead = false;
            state.id_ex_stage.memWrite = false;
            state.id_ex_stage.regWrite = true;
            state.id_ex_stage.memToReg = false;
            break;
        case OP_LW: 
            state.id_ex_stage.regDst = false;
            state.id_ex_stage.ALUOp1 = false;
            state.id_ex_stage.ALUOp2 = false;
            state.id_ex_stage.ALUSrc = true;
            state.id_ex_stage.branch = false;
            state.id_ex_stage.memRead = true;
            state.id_ex_stage.memWrite = false;
            state.id_ex_stage.regWrite = true;
            state.id_ex_stage.memToReg = true;
            break;
        case OP_SW:
            state.id_ex_stage.regDst = false;
            state.id_ex_stage.ALUOp1 = false;
            state.id_ex_stage.ALUOp2 = false;
            state.id_ex_stage.ALUSrc = true;
            state.id_ex_stage.branch = false;
            state.id_ex_stage.memRead = false;
            state.id_ex_stage.memWrite = true;
            state.id_ex_stage.regWrite = false;
            state.id_ex_stage.memToReg = false; 
            break;
        case OP_BEQ:
            state.id_ex_stage.regDst = false;
            state.id_ex_stage.ALUOp1 = false;
            state.id_ex_stage.ALUOp2 = true;
            state.id_ex_stage.ALUSrc = false;
            state.id_ex_stage.branch = true;
            state.id_ex_stage.memRead = false;
            state.id_ex_stage.memWrite = false;
            state.id_ex_stage.regWrite = false;
            state.id_ex_stage.memToReg = false;
            break;
        default:
            state.id_ex_stage.regDst = false;
            state.id_ex_stage.ALUOp1 = false;
            state.id_ex_stage.ALUOp2 = false;
            state.id_ex_stage.ALUSrc = false;
            state.id_ex_stage.branch = false;
            state.id_ex_stage.memRead = false;
            state.id_ex_stage.memWrite = false;
            state.id_ex_stage.regWrite = false;
            state.id_ex_stage.memToReg = false;
    }
}

// *------------------------------------------------------------*
// Instruction Execution Helpers TODO
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
    decodeInst(instr, decodedInst);
    uint32_t readReg1 = regs[decodedInst.rs];
    uint32_t readReg2 = regs[decodedInst.rt];
    state.hzd -> jump = false;  // erase previously written value 

    // to do: 
    // 1) Sign extending immediate and slt << 2 (jump)
    // branch_addr = (signExtImm << 2)
    // 2) Branch ALU + readReg comparison


    // if branch -> calculate address and check condition
    // if load_use hazard -> stall
    state.hzd -> checkHazard(state, decodedInst);

    // Update state
    state.id_ex_stage.decodedInst = decodedInst;
    state.id_ex_stage.npc = state.if_id_stage.npc;
    state.id_ex_stage.readData1 = readReg1;
    state.id_ex_stage.readData2 = readReg2;

    
    // FLUSH IF_ID IF STALL
    if (state.stall) {
        state.if_id_stage.instr = 0;
        state.if_id_stage.npc = 0;
    }
}


void EX(STATE & state)
{
    // need to do forwarding
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
    switch(state.id_ex_stage.decodedInst.op){
        case OP_ZERO: //R tytpe 
            executor.executeR(state);
            break;
        case OP_J:
            break;
        case OP_JAL:
            break;
        case OP_BEQ:    // branch done in the ID
            break;
        case OP_BNE:    // branch done in the ID
            break;
        default: //I type not BNE and BEQ
            executor.executeI(state);
            break;
    }


    state.ex_mem_stage.decodedInst = state.id_ex_stage.decodedInst;
    state.ex_mem_stage.npc = state.id_ex_stage.npc;
    state.ex_mem_stage.memoryAddr = state.id_ex_stage.readData2; 
    // wrote to state.ex_mem.aluResult in execute(), do not do it here
}


void MEM(STATE & state){
    state.mem_wb_stage.decodedInst = state.ex_mem_stage.decodedInst;
    state.mem_wb_stage.aluResult = state.ex_mem_stage.aluResult;
    state.mem_wb_stage.data = state.ex_mem_stage.aluResult;

    // Do actual memory stuff

}


void WB(STATE & state){
    // do not have to store anything after WB

    // Do actual write back stuff
}

// *------------------------------------------------------------*


// Driver stuff
int initMemory(ifstream &inputProg)
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
                cout << "Could not set memory value!" << endl;
                return -EINVAL;
            }

            // We're reading 4 bytes each time...
            addr += 4;
        }
    }
    else
    {
        cout << "Invalid file stream or memory image passed, could not initialise memory values" << endl;
        return -EINVAL;
    }

    return 0;
}

// Main function
int main(int argc, char *argv[])
{
    // Initialize state ()
    STATE state = {};
    // Initialize registers
    for(int i = 0; i < 32; i++){ regs[i] = 0;
    }
    // Initialize memory
    // if (argc != 2)
    // {
    //     cout << "Usage: ./cycle_sim <file name>" << endl;
    //     return -EINVAL;
    // }

    ifstream prog;
    prog.open(argv[1], ios::binary | ios::in);

    mem = createMemoryStore();

    if (initMemory(prog))
    {
        return -EBADF;
    }

    // Step through the five stages
    while (true)
    {
        // Print state
        printState(state, std::cout, false);
        // Execute stages
        WB(state);
        MEM(state);
        EX(state);
        ID(state);
        // check state.stall. Don't fetch or increment PC if state.stall = true, add nop
        IF(state);
        // Update state
        state.pc = state.if_stage.npc; //Not right
        state.cycles++;
        // If pc > 16 exit
        if(state.pc > 16) break;
    }
    // Print final state
    printState(state, std::cout, true);

    // Dump memery
    dumpMemoryState(mem);
    
}





