#include <set>

#ifndef UTILITYENUM_H
#define UTILITYENUM_H

// Registers
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

enum HAZARD_TYPE{
    NONE, EX_HAZ, MEM_HAZ
};

std::set<int> VALID_OP = {OP_ZERO, OP_ADDI,  OP_ADDIU, OP_ANDI,  OP_BEQ, OP_BNE, OP_LBU, OP_LHU, OP_LL, OP_LUI, 
                        OP_LW, OP_ORI, OP_SLTI, OP_SLTIU, OP_SB, OP_SC, OP_SH, OP_SW, OP_J, OP_JAL};
std::set<int> I_TYPE_NO_LS = {OP_ADDI,  OP_ADDIU, OP_ANDI,  OP_BEQ, OP_BNE, OP_ORI, OP_SLTI, OP_SLTIU};
std::set<int> LOAD_OP = {OP_LL, OP_LUI, OP_LW, OP_LHU, OP_LBU};
std::set<int> STORE_OP = {OP_SB, OP_SC, OP_SH, OP_SW};
std::set<int> JB_OP = {OP_BEQ, OP_BNE, OP_J, OP_JAL};
#endif