
#include "CacheConfig.h"
#include "MemoryStore.h"
#include <math.h>
#include <vector>

using byte_t = uint32_t;

struct Block {
	uint32_t tag;
	uint32_t lastUsed;
	bool valid;
	bool dirty;
	std::vector<byte_t> data;	
};


class Cache {
private:
	CacheConfig cfg;	
	MemoryStore* mem;
	uint32_t n, entries, tag_bits, index_bits, offset_bits, use_counter, hits, miss;
	std::vector<std::vector<Block>> cache;
		
	uint32_t getTag(uint32_t addr) {
		return addr >> (32 - tag_bits);
	}

	uint32_t getIndex(uint32_t addr) {
		return (addr << (tag_bits)) >> (32 - index_bits);
	}

	uint32_t getOffset(uint32_t addr) {
		return (addr << (32 - offset_bits)) >> (32 - offset_bits);
	}
	// evicts a block from the cache, returns the index of the evicted block - cache[i][return_value]
	uint32_t evict(uint32_t addr) {
		auto index = getIndex(addr), off = getOffset(addr);
		uint32_t ret = 0, last = UINT32_MAX;

		for (uint32_t i = 0; i < n; ++i) {
			if (!cache[index][i].valid) {	// cache[index] has invalid block -> evict it
				ret = i;
				break;
			} 
			if (cache[index][i].lastUsed < last) {
				ret = i;
				last = cache[index][i].lastUsed;
			}
		}
		// write-back for the evicted block	
		if (cache[index][ret].dirty) {
			for (uint32_t i = 0; i < cfg.blockSize; ++i) {
				mem->setMemValue(addr - off + i, cache[index][ret].data[i], BYTE_SIZE);
			}
		}
		return ret;
	}

	// brings block from memory and put it in the cache. Return the index of the evicted block - cache[i][return_value]
	uint32_t bringFromMemory(uint32_t addr) {
		auto index = getIndex(addr), off = getOffset(addr), where = evict(addr);
		Block newBlock {getTag(addr), ++use_counter, true, false, std::vector<byte_t>(cfg.blockSize)};
		for (uint32_t i = 0; i < cfg.blockSize; ++i) {
			mem -> getMemValue(addr - off + i, newBlock.data[i], BYTE_SIZE);
		}
		cache[index][where] = newBlock;
		return where;
	}


	byte_t getByte(uint32_t addr) {
		auto tag = getTag(addr), index = getIndex(addr), off = getOffset(addr);
		// check blocks at the given index
		for (uint32_t i = 0; i < n; ++i) {
			if (cache[index][i].tag == tag && cache[index][i].valid) { 
				++hits;
				cache[index][i].lastUsed = ++use_counter; // hit
				return cache[index][i].data[off];
			} 
		}
		// miss
		++miss;
		auto where = bringFromMemory(addr);
		return cache[index][where].data[off];
	}

	void setByte(uint32_t addr, byte_t value) {
		auto tag = getTag(addr), index = getIndex(addr), off = getOffset(addr);
		// check blocks at the given index
		for (uint32_t i = 0; i < n; ++i) {
			if (cache[index][i].tag == tag && cache[index][i].valid) { 
					hits++;	
					cache[index][i].lastUsed = ++use_counter; // hit
					cache[index][i].data[off] = value;
					cache[index][i].dirty = true;
					return;
			} 
		}
		// miss
		miss++;
		auto where = bringFromMemory(addr);
		cache[index][where].data[off] = value;
		cache[index][where].dirty = true;
	}
public:
	Cache(const CacheConfig& cfg, MemoryStore* mem): cfg(cfg),  mem(mem) {
		n = (cfg.type == DIRECT_MAPPED) ? 1 : 2;
		entries = cfg.cacheSize / (cfg.blockSize * n);
		cache.resize(entries);
		offset_bits = log2(cfg.blockSize);
		index_bits = log2(entries);
		tag_bits = 32 - offset_bits - index_bits;
		use_counter = hits = miss = 0;
		for (uint32_t i = 0; i < entries; ++i) {
			for (uint32_t j = 0; j < n; ++j) {
				cache[i].push_back(Block{0, 0, false, false, std::vector<byte_t>(cfg.blockSize)});
			}
		}
	}

	uint32_t getCacheValue(uint32_t addr, uint32_t& value, MemEntrySize size) {
		value = 0;
		for (int i = 0; i < size; ++i) {
			value <<= 8;
			value += getByte(addr + i);
		}
		return value;
	}

	void setCacheValue(uint32_t addr, uint32_t value, MemEntrySize size) {
		for (int i = 0; i < size; ++i) {
			setByte(addr + i, (value << (8 * i)) >> (8 * size));
		}
	}

	uint32_t getHits() {
		return hits;
	}
	uint32_t getMisses() {
		return miss;
	}
};

