#pragma once
#include <memory>
#include <vector>
#include "控制流图.h"

class DominatorTree {
public:
    void build(CFG& cfg);

    bool dominates(const BasicBlock* a, const BasicBlock* b) const;
    BasicBlock* findLoopHeader(const BasicBlock* backEdgeSrc,
                               const BasicBlock* backEdgeDst) const;
    std::vector<BasicBlock*> getNaturalLoop(BasicBlock* header,
                                            BasicBlock* backEdgeSrc) const;

private:
    void dfs(CFG& cfg, BasicBlock* block, int& time);
};