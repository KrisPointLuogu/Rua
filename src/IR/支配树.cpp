#include "IR/支配树.h"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <stack>
#include <unordered_set>

void DominatorTree::build(CFG& cfg) {
    if (cfg.blockCount() == 0) return;

    for (auto& b : cfg.blocks) {
        b->domDepth = 0;
        b->idom = nullptr;
        b->domChildren.clear();
    }

    int time = 0;
    dfs(cfg, cfg.entryBlock, time);

    bool changed = true;
    int n = cfg.blockCount();

    cfg.entryBlock->idom = cfg.entryBlock;

    while (changed) {
        changed = false;
        for (auto& b : cfg.blocks) {
            if (b.get() == cfg.entryBlock) continue;
            BasicBlock* newIdom = nullptr;
            for (auto* pred : b->predecessors) {
                if (!pred->idom) continue;
                if (!newIdom) {
                    newIdom = pred;
                } else {
                    BasicBlock* finger1 = pred;
                    BasicBlock* finger2 = newIdom;
                    while (finger1 != finger2) {
                        auto& f1io = finger1->dfsIn;
                        auto& f1oo = finger1->dfsOut;
                        auto& f2io = finger2->dfsIn;
                        auto& f2oo = finger2->dfsOut;
                        if (!f1io.empty() && !f2io.empty()) {
                            while (f1io[0] > f2io[0] || (f1io[0] == f2io[0] && f1oo[0] < f2oo[0])) {
                                finger1 = finger1->idom;
                                if (!finger1) break;
                            }
                            if (!finger1) break;
                            while (f2io[0] > f1io[0] || (f2io[0] == f1io[0] && f2oo[0] < f1oo[0])) {
                                finger2 = finger2->idom;
                                if (!finger2) break;
                            }
                            if (!finger2) break;
                        }
                    }
                    if (finger1 && finger1 == finger2)
                        newIdom = finger1;
                }
            }
            if (newIdom && newIdom != b->idom) {
                b->idom = newIdom;
                changed = true;
            }
        }
    }

    cfg.entryBlock->idom = nullptr;

    for (auto& b : cfg.blocks) {
        if (b->idom) b->idom->domChildren.push_back(b.get());
    }

    for (auto& b : cfg.blocks) {
        if (b->idom)
            b->domDepth = b->idom->domDepth + 1;
    }

    for (auto& b : cfg.blocks) {
        if (b->predecessors.size() < 2) continue;
        for (auto* pred : b->predecessors) {
            if (dominates(b.get(), pred)) {
                b->loopDepth = (b->idom ? b->idom->loopDepth : -1) + 1;
                break;
            }
        }
    }
}

void DominatorTree::dfs(CFG& cfg, BasicBlock* block, int& time) {
    block->dfsIn.push_back(time++);
    for (auto* succ : block->successors) {
        if (succ->dfsIn.empty())
            dfs(cfg, succ, time);
    }
    block->dfsOut.push_back(time++);
}

bool DominatorTree::dominates(const BasicBlock* a, const BasicBlock* b) const {
    if (!a || !b) return false;
    if (a == b) return true;
    if (a->dfsIn.empty() || b->dfsIn.empty()) return false;
    return a->dfsIn[0] <= b->dfsIn[0] && a->dfsOut[0] >= b->dfsOut[0];
}

BasicBlock* DominatorTree::findLoopHeader(const BasicBlock* backEdgeSrc,
                                           const BasicBlock* backEdgeDst) const {
    if (dominates(const_cast<BasicBlock*>(backEdgeDst),
                  const_cast<BasicBlock*>(backEdgeSrc)))
        return const_cast<BasicBlock*>(backEdgeDst);
    return nullptr;
}

std::vector<BasicBlock*> DominatorTree::getNaturalLoop(BasicBlock* header,
                                                        BasicBlock* backEdgeSrc) const {
    std::vector<BasicBlock*> loop;
    std::unordered_set<BasicBlock*> visited;
    std::stack<BasicBlock*> stack;

    stack.push(backEdgeSrc);
    visited.insert(backEdgeSrc);

    while (!stack.empty()) {
        auto* block = stack.top();
        stack.pop();
        loop.push_back(block);
        if (block == header) continue;
        for (auto* pred : block->predecessors) {
            if (visited.insert(pred).second)
                stack.push(pred);
        }
    }
    return loop;
}