#pragma once
#include <memory>
#include <string>
#include <vector>
#include "中间指令.h"

struct BasicBlock {
    int id = -1;
    std::vector<std::unique_ptr<TACInst>> instructions;
    std::vector<BasicBlock*> predecessors;
    std::vector<BasicBlock*> successors;

    int loopDepth = -1;
    int domDepth = 0;
    BasicBlock* idom = nullptr;
    std::vector<BasicBlock*> domChildren;

    std::vector<int> dfsIn, dfsOut;

    BasicBlock() = default;
    explicit BasicBlock(int id) : id(id) {}

    bool isLoopHeader() const { return loopDepth >= 0; }
};