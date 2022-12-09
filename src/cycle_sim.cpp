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

// TODO HAZARDS AND FORWARDING

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
    uint32_t readReg1;
    uint32_t readReg2;
    uint32_t regDest;
    uint32_t readData1;
    uint32_t readData2;

    bool regWrite;
};    

struct EX_MEM_STAGE{
    DecodedInst decodedInst;
    uint32_t npc; 
    uint32_t readData1;
    uint32_t readData2;
    uint32_t regDest;
    uint32_t aluResult;

    // Control 
    bool regWrite;
    bool ALUOp1;
    bool ALUOp2;
    bool ALUSrc;
};

struct MEM_WB_STAGE{
    DecodedInst decodedInst;
    uint32_t regDest;
    uint32_t aluResult;
    uint32_t data; 

    // Control
    bool regWrite;
    bool memRead;
    bool memWrite;
    bool branch;
};


// HAZARD DETECTION AND FORWARDING

enum class HAZARD_TYPE{
    NONE, EX, MEM
};



struct FORWARD_UNIT 
{
    HAZARD_TYPE forward1;
    HAZARD_TYPE forward2;

    void checkHazardEX(STATE& state) {
        if (checkEX1(state)) {
            forward1 = HAZARD_TYPE::EX;
        }
        if (checkEX2(state)) {
            forward2 = HAZARD_TYPE::EX;
        }
    }

    void checkHazardMEM(STATE& state) {
        if (checkMEM1(state) && !checkEX1(state)) {
            forward1 = HAZARD_TYPE::MEM;
        }
        if (checkEX2(state) && !checkEX2(state)) {
            forward2 = HAZARD_TYPE::MEM;
        }
    }


private:

    bool checkEX1(STATE& state) {
        return state.ex_mem_stage.regWrite && (state.ex_mem_stage.regDest != 0) &&
            (state.ex_mem_stage.regDest == state.id_ex_stage.readReg1);
    }

    bool checkEX2(STATE& state) {
        return state.ex_mem_stage.regWrite && (state.ex_mem_stage.regDest != 0) &&
            (state.ex_mem_stage.regDest == state.id_ex_stage.readReg2);
    }


    bool checkMEM1(STATE& state) {
        return state.mem_wb_stage.regWrite && (state.mem_wb_stage.regDest != 0) &&
            (state.mem_wb_stage.regDest == state.id_ex_stage.readReg1);
    }

    bool checkMEM2(STATE& state) {
        return state.mem_wb_stage.regWrite && (state.mem_wb_stage.regDest != 0) &&
            (state.mem_wb_stage.regDest == state.id_ex_stage.readReg2); 
    }
};



struct STATE
{
    uint32_t pc;
    uint32_t cycles;
    IF_ID_STAGE if_id_stage;
    ID_EX_STAGE id_ex_stage;
    EX_MEM_STAGE ex_mem_stage;
    MEM_WB_STAGE mem_wb_stage;
    // added by Amir
    FORWARD_UNIT fwd;
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
// *------------------------------------------------------------*
// Update Control Signal Helpers
// *------------------------------------------------------------*

// Updates control signals  for exec stage
void execControl(STATE& state){
    // Control Logic
    switch(state.ex_mem_stage.decodedInst.op){
        case OP_RTYPE:
            state.ex_mem_stage.regDst = true;
            state.ex_mem_stage.ALUOp1 = true;
            state.ex_mem_stage.ALUOp2 = false;
            state.ex_mem_stage.ALUSrc = false;
            break;
        //TODO more 
    }
}

// Instruction Execution Helpers TODO
// *------------------------------------------------------------*


// *------------------------------------------------------------*



// Function for each stage
// *------------------------------------------------------------*
void IF(STATE & state){
    // Read instruction from memory
    uint32_t instr = 0;
    // TODO ERROR HANDLING
    mem->getMemValue(state.pc, instr, WORD_SIZE);
    // Update state
    state.if_id_stage.instr = instr;
    state.if_id_stage.npc = state.pc + 4;
}


void ID(STATE& state){
    
    // Read instruction from IF stage
    uint32_t instr = state.if_id_stage.instr;
    
    // Decode instruction
    DecodedInst decodedInst;
    decodeInst(instr, decodedInst);

    // Read registers
    uint32_t readReg1 = regs[decodedInst.rs];
    uint32_t readReg2 = regs[decodedInst.rt];

    // Update state
    state.id_ex_stage.decodedInst = decodedInst;
    state.id_ex_stage.npc = state.if_id_stage.npc;
    state.id_ex_stage.readReg1 = readReg1;
    state.id_ex_stage.readReg2 = readReg2;
    state.id_ex_stage.readData1 = readReg1;
    state.id_ex_stage.readData2 = readReg2;
}


void EX(STATE & state){
   // Need todo ALU stuff + control 

    // Do instruction specific stuff
    doLoad(state);

    state.ex_mem_stage.decodedInst = state.id_ex_stage.decodedInst;
    state.ex_mem_stage.npc = state.id_ex_stage.npc;
    state.ex_mem_stage.readData2 = state.id_ex_stage.readData2; 
}


void MEM(STATE & state){
    state.mem_wb_stage.decodedInst = state.ex_mem_stage.decodedInst;
    state.mem_wb_stage.aluResult = state.ex_mem_stage.aluResult;
    state.mem_wb_stage.data = state.ex_mem_stage.aluResult;

    // Do actual memory stuff

}


void WB(STATE & state){
    state.wb_stage.decodedInst = state.mem_stage.decodedInst;
    state.wb_stage.data = state.mem_stage.data;

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





