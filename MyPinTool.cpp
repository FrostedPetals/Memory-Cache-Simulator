#include "pin.H"
#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
using std::cerr;
using std::endl;
using std::string;

UINT64 insCount = 0;
UINT64 bblCount = 0;
UINT64 threadCount = 0;
ADDRINT mainLow = 0, mainHigh = 0;
const UINT64 LIMIT = 50000;

std::ostream *out = &cerr;
std::ofstream TraceFile;
std::ofstream JsonFile;

struct TraceEntry
{
    char type;
    UINT64 addr;
    bool hitLRU;
    bool hitFIFO;
    bool hitLFU;
};

std::vector<TraceEntry> traceLog;

struct CacheLine
{
    bool valid;
    UINT64 tag;
    UINT64 lastUsed;
    UINT64 insertedAt;
    UINT64 frequency;
};

struct CacheSet
{
    std::vector<CacheLine> lines;
};

enum Policy { LRU, FIFO, LFU };

class Cache
{
public:
    int cacheSize, blockSize, associativity;
    int numSets, offsetBits, indexBits;

    std::vector<CacheSet> sets;

    UINT64 hits = 0, misses = 0;
    UINT64 globalCounter = 0;
    Policy policy;

    Cache(int c, int b, int a, Policy p)
    {
        cacheSize = c;
        blockSize = b;
        associativity = a;
        policy = p;

        numSets = cacheSize / (blockSize * associativity);

        offsetBits = (int)log2(blockSize);
        indexBits = (int)log2(numSets);

        sets.resize(numSets);
        for (int i = 0; i < numSets; i++)
        {
            sets[i].lines.resize(associativity);
            for (int j = 0; j < associativity; j++)
            {
                sets[i].lines[j].valid = false;
                sets[i].lines[j].tag = 0;
                sets[i].lines[j].lastUsed = 0;
                sets[i].lines[j].insertedAt = 0;
                sets[i].lines[j].frequency = 0;
            }
        }
    }

    UINT64 getIndex(UINT64 addr)
    {
        return (addr >> offsetBits) & ((1 << indexBits) - 1);
    }

    UINT64 getTag(UINT64 addr)
    {
        return addr >> (offsetBits + indexBits);
    }

    bool access(UINT64 addr){
        globalCounter++;

        UINT64 index = getIndex(addr);
        UINT64 tag = getTag(addr);

        // check if it's a hit
        for (auto &line : sets[index].lines)
        {
            if (line.valid && line.tag == tag)
            {
                hits++;

                // update metadata on hit
                line.lastUsed = globalCounter;
                line.frequency++;

                return true;
            }
        }

        // miss
        misses++;

        // if empty, fill it
        for (auto &line : sets[index].lines)
        {
            if (!line.valid)
            {
                line.valid = true;
                line.tag = tag;
                line.lastUsed = globalCounter;
                line.insertedAt = globalCounter;
                line.frequency = 1;
                return false;
            }
        }

        // choose victim
        int victim = 0;

        if (policy == LRU)
        {
            UINT64 minTime = sets[index].lines[0].lastUsed;

            for (int i = 1; i < associativity; i++)
            {
                if (sets[index].lines[i].lastUsed < minTime)
                {
                    minTime = sets[index].lines[i].lastUsed;
                    victim = i;
                }
            }
        }
        else if (policy == FIFO)
        {
            UINT64 minTime = sets[index].lines[0].insertedAt;

            for (int i = 1; i < associativity; i++)
            {
                if (sets[index].lines[i].insertedAt < minTime)
                {
                    minTime = sets[index].lines[i].insertedAt;
                    victim = i;
                }
            }
        }
        else if (policy == LFU)
        {
            UINT64 minFreq = sets[index].lines[0].frequency;
            UINT64 oldest = sets[index].lines[0].insertedAt;

            for (int i = 1; i < associativity; i++)
            {
                UINT64 freq = sets[index].lines[i].frequency;
                UINT64 ins  = sets[index].lines[i].insertedAt;

                if (freq < minFreq ||
                (freq == minFreq && ins < oldest))
                {
                    minFreq = freq;
                    oldest = ins;
                    victim = i;
                }
            }
        }

        sets[index].lines[victim].valid = true;
        sets[index].lines[victim].tag = tag;
        sets[index].lines[victim].lastUsed = globalCounter;
        sets[index].lines[victim].insertedAt = globalCounter;
        sets[index].lines[victim].frequency = 1;

        return false;
    }
};

Cache *cacheLRU;
Cache *cacheFIFO;
Cache *cacheLFU;

VOID ImageLoad(IMG img, VOID *v)
{
    if (IMG_IsMainExecutable(img))
    {
       RTN mainRtn = RTN_FindByName(img, "main");
        if (RTN_Valid(mainRtn))
        {
            mainLow  = RTN_Address(mainRtn);
            mainHigh = mainLow + RTN_Size(mainRtn);
            cerr << "main() range: " << std::hex
                 << mainLow << " - " << mainHigh << endl;
        }
    }
}

VOID RecordMemRead(VOID *ip, VOID *addr)
{
    UINT64 a = (UINT64)addr;

    bool h1 = cacheLRU->access(a);
    bool h2 = cacheFIFO->access(a);
    bool h3 = cacheLFU->access(a);

    if (traceLog.size() < LIMIT) {
        traceLog.push_back({'R', a, h1, h2, h3});
        //TraceFile << "R " << ip << " " << addr << endl;

        TraceFile << "{";
        TraceFile << "\"type\":\"R\",";
        TraceFile << "\"addr\":" << a << ",";
        TraceFile << "\"hitLRU\":" << (h1 ? "true" : "false") << ",";
        TraceFile << "\"hitFIFO\":" << (h2 ? "true" : "false") << ",";
        TraceFile << "\"hitLFU\":" << (h3 ? "true" : "false");
        TraceFile << "}\n";
    }
}

VOID RecordMemWrite(VOID *ip, VOID *addr)
{
    UINT64 a = (UINT64)addr;

    bool h1 = cacheLRU->access(a);
    bool h2 = cacheFIFO->access(a);
    bool h3 = cacheLFU->access(a);

    if (traceLog.size() < LIMIT) {
        traceLog.push_back({'W', a, h1, h2, h3});

        //TraceFile << "W " << ip << " " << addr << endl;

        TraceFile << "{";
        TraceFile << "\"type\":\"W\",";
        TraceFile << "\"addr\":" << a << ",";
        TraceFile << "\"hitLRU\":" << (h1 ? "true" : "false") << ",";
        TraceFile << "\"hitFIFO\":" << (h2 ? "true" : "false") << ",";
        TraceFile << "\"hitLFU\":" << (h3 ? "true" : "false");
        TraceFile << "}\n";
    }
}


VOID Instruction(INS ins, VOID *v)
{
    ADDRINT ip = INS_Address(ins);

    if (ip < mainLow || ip > mainHigh)
        return;

    if (INS_IsMemoryRead(ins) && !INS_IsStackPointerRelative(ins))
    {
        INS_InsertCall(ins, IPOINT_BEFORE,
                       (AFUNPTR)RecordMemRead,
                       IARG_INST_PTR,
                       IARG_MEMORYREAD_EA,
                       IARG_END);
    }

    if (INS_IsMemoryWrite(ins) && !INS_IsStackPointerRelative(ins))
    {
        INS_InsertCall(ins, IPOINT_BEFORE,
                       (AFUNPTR)RecordMemWrite,
                       IARG_INST_PTR,
                       IARG_MEMORYWRITE_EA,
                       IARG_END);
    }
}


VOID CountBbl(UINT32 numInstInBbl)
{
    bblCount++;
    insCount += numInstInBbl;
}

VOID Trace(TRACE trace, VOID *v)
{
    for (BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl))
    {
        BBL_InsertCall(bbl, IPOINT_BEFORE, (AFUNPTR)CountBbl,
                       IARG_UINT32, BBL_NumIns(bbl), IARG_END);
    }
}

VOID ThreadStart(THREADID threadIndex, CONTEXT *ctxt, INT32 flags, VOID *v)
{
    threadCount++;
}


VOID Fini(INT32 code, VOID *v)
{
    TraceFile.close();

    *out << "=========== RESULTS ===========" << endl;

    *out << "LRU Hits: " << cacheLRU->hits << endl;
    *out << "LRU Misses: " << cacheLRU->misses << endl;

    *out << "FIFO Hits: " << cacheFIFO->hits << endl;
    *out << "FIFO Misses: " << cacheFIFO->misses << endl;

    *out << "LFU Hits: " << cacheLFU->hits << endl;
    *out << "LFU Misses: " << cacheLFU->misses << endl;

    JsonFile << "{\n";

    JsonFile << "  \"LRU\": {\n";
    JsonFile << "    \"hits\": " << cacheLRU->hits << ",\n";
    JsonFile << "    \"misses\": " << cacheLRU->misses << "\n";
    JsonFile << "  },\n";

    JsonFile << "  \"FIFO\": {\n";
    JsonFile << "    \"hits\": " << cacheFIFO->hits << ",\n";
    JsonFile << "    \"misses\": " << cacheFIFO->misses << "\n";
    JsonFile << "  },\n";

    JsonFile << "  \"LFU\": {\n";
    JsonFile << "    \"hits\": " << cacheLFU->hits << ",\n";
    JsonFile << "    \"misses\": " << cacheLFU->misses << "\n";
    JsonFile << "  }\n";

    JsonFile << "}\n";

    JsonFile.close();
}

int main(int argc, char *argv[])
{
    PIN_InitSymbols();
    if (PIN_Init(argc, argv))
        return -1;

    TraceFile.open("memtrace.ndjson");
    JsonFile.open("UIoutput.json");

    
    cacheLRU  = new Cache(64, 8, 2, LRU);
    cacheFIFO = new Cache(64, 8, 2, FIFO);
    cacheLFU  = new Cache(64, 8, 2, LFU);
    
    TRACE_AddInstrumentFunction(Trace, 0);
    INS_AddInstrumentFunction(Instruction, 0);
    IMG_AddInstrumentFunction(ImageLoad, 0);
    PIN_AddThreadStartFunction(ThreadStart, 0);
    PIN_AddFiniFunction(Fini, 0);

    PIN_StartProgram();
    return 0;
}
