#include <stdint.h>


struct CONTROL 
{
    bool regDst;
    bool ALUOp1;
    bool ALUOp2;
    bool ALUSrc;
    bool branch;
    bool memRead;
    bool memWrite;
    bool regWrite;
    bool memToReg;
};
auto CONTROL_RTYPE = CONTROL{true, true, true, false, false, false, false, true, false};
auto CONTROL_LOAD = CONTROL{false, false, false, true, false, true, false, true, true};
auto CONTROL_STORE = CONTROL{false, false, false, true, false, false, true, false, false};
auto CONTROL_ITYPE = CONTROL{false, true, false, false, false, false, false, true, false};
auto CONTROL_NOP = CONTROL{false, false, false, false, false, false, false, false, false};


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

uint32_t instructBits(uint32_t instruct, int start, int end)
{
    int run = start - end + 1;
    uint32_t mask = (1 << run) - 1;
    uint32_t clipped = instruct >> end;
    return clipped & mask;
}

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
/*-----------------------------
 * -----Pipeline Stages--------
 ----------------------------*/
struct IF_ID_STAGE{
    uint32_t instr;
    uint32_t npc;
};

struct ID_EX_STAGE{
    DecodedInst decodedInst;
    uint32_t npc;
    uint32_t readData1;
    uint32_t readData2;
    CONTROL ctrl;
};    

struct EX_MEM_STAGE{
    DecodedInst decodedInst;
    uint32_t npc; 
    uint32_t aluResult;
    uint32_t setMemValue;
    CONTROL ctrl;
};

struct MEM_WB_STAGE{
    DecodedInst decodedInst;
    uint32_t aluResult;
    uint32_t data; 
    CONTROL ctrl;
};

