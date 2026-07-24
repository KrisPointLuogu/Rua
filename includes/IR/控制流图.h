#pragma once
#include <memory>
#include <string>
#include <vector>
#include "基本块.h"

class CFG {
public:
    std::vector<std::unique_ptr<BasicBlock>> blocks;
    BasicBlock* entryBlock = nullptr;

    void buildFromTAC(std::vector<std::unique_ptr<TACInst>>& linearTAC);
    void recomputeFromScratch(std::vector<std::unique_ptr<TACInst>>& linearTAC);

    BasicBlock* getBlock(int id) const;
    int blockCount() const { return static_cast<int>(blocks.size()); }

    void removeBlock(int id);
    void print() const;
};