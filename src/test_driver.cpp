#include <iostream>
#include <iomanip>
#include <fstream>
#include <errno.h>
#include "MemoryStore.h"
#include "RegisterInfo.h"
#include "EndianHelpers.h"
#include "DriverFunctions.h"

using namespace std;

static MemoryStore *mem;

int initMemory(ifstream & inputProg)
{
    if(inputProg && mem)
    {
        uint32_t curVal = 0;
        uint32_t addr = 0;

        while(inputProg.read((char *)(&curVal), sizeof(uint32_t)))
        {
            curVal = ConvertWordToBigEndian(curVal);
            int ret = mem->setMemValue(addr, curVal, WORD_SIZE);

            if(ret)
            {
                cout << "Could not set memory value!" << endl;
                return -EINVAL;
            }

            //We're reading 4 bytes each time...
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

int main(int argc, char **argv)
{
    if(argc != 2)
    {
        cout << "Usage: ./cycle_sim <file name>" << endl;
        return -EINVAL;
    }

    ifstream prog;
    prog.open(argv[1], ios::binary | ios::in);

    mem = createMemoryStore();

    if(initMemory(prog))
    {
        return -EBADF;
    }

    CacheConfig icConfig;
    icConfig.cacheSize = 64;
    icConfig.blockSize = 4;
    icConfig.type = TWO_WAY_SET_ASSOC;
    icConfig.missLatency = 3;
    CacheConfig dcConfig = icConfig;

    initSimulator(icConfig, dcConfig, mem);

    runCycles(1);
    runCycles(1);
    runCycles(1);
    runCycles(1);
    runCycles(7);
    runCycles(1);
    runCycles(1);
    runCycles(20);
    runTillHalt();
    finalizeSimulator();
    delete mem;
    return 0;
}
