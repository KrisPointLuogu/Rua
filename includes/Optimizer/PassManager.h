#pragma once
#include "IR/TACInstructions.h"
#include "IR/CFG.h"
#include "IR/DominatorTree.h"
#include <algorithm>
#include <iostream>
#include <numeric>

// Fix jump targets after instruction list modifications.
// oldToNew[oldIndex] = newIndex; -1 means instruction was removed.
inline void fixJumpTargets(TACFunction& func, const std::vector<int>& oldToNew) {
    for (auto& inst : func.instructions) {
        auto op = inst->getOpcode();
        if (op == TACOpcode::JMP) {
            auto* j = static_cast<TACJmp*>(inst.get());
            if (j->targetBlock >= 0 && j->targetBlock < static_cast<int>(oldToNew.size())) {
                int mapped = oldToNew[j->targetBlock];
                j->targetBlock = mapped;
            }
        } else if (op == TACOpcode::JIF) {
            auto* j = static_cast<TACJif*>(inst.get());
            if (j->targetBlock >= 0 && j->targetBlock < static_cast<int>(oldToNew.size())) {
                int mapped = oldToNew[j->targetBlock];
                j->targetBlock = mapped;
            }
            if (j->fallBlock >= 0 && j->fallBlock < static_cast<int>(oldToNew.size())) {
                int mapped = oldToNew[j->fallBlock];
                j->fallBlock = mapped;
            }
        }
    }
}

// Build a remapping from old instruction count + a list of (position, netInsertions).
// netInsertions > 0 means instructions were inserted at that position;
// netInsertions < 0 means instructions were deleted starting at that position.
// Returns oldToNew mapping where oldToNew[oldIndex] = newIndex, or -1 if removed.
inline std::vector<int> buildRemapping(int oldSize, std::vector<std::pair<int,int>> changes) {
    // Sort changes by position descending so insertions/deletions at higher indices
    // don't affect the positions of lower-index changes
    std::sort(changes.begin(), changes.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });

    // Build a list of net shifts: for each old index, how much it shifted
    std::vector<int> oldToNew(oldSize);
    std::iota(oldToNew.begin(), oldToNew.end(), 0);

    // Apply changes from back to front
    for (auto& [pos, delta] : changes) {
        if (delta > 0) {
            // Insertions at pos: shift everything at/after pos right by delta
            for (int i = pos; i < oldSize; i++) {
                if (oldToNew[i] >= pos)
                    oldToNew[i] += delta;
            }
        } else if (delta < 0) {
            // Deletions at pos: shift everything after pos left by |delta|
            int count = -delta;
            for (int i = 0; i < oldSize; i++) {
                if (oldToNew[i] >= pos + count)
                    oldToNew[i] -= count;
                else if (oldToNew[i] >= pos && i >= pos && i < pos + count)
                    oldToNew[i] = -1; // deleted
            }
        }
    }
    return oldToNew;
}

class OptimizerPass {
public:
    virtual ~OptimizerPass() = default;
    virtual const char* name() const = 0;
    virtual bool run(TACProgram& program, int funcIdx) = 0;
};

class ConstantPropagation : public OptimizerPass {
public:
    const char* name() const override { return "常量传播"; }
    bool run(TACProgram& program, int funcIdx) override;
};

class DeadCodeElimination : public OptimizerPass {
public:
    const char* name() const override { return "死代码消除"; }
    bool run(TACProgram& program, int funcIdx) override;
};

class LoopInvariantCodeMotion : public OptimizerPass {
public:
    const char* name() const override { return "循环不变量外提"; }
    bool run(TACProgram& program, int funcIdx) override;
};

class LoopUnrolling : public OptimizerPass {
public:
    const char* name() const override { return "循环展开"; }
    bool run(TACProgram& program, int funcIdx) override;
};

class FunctionInlining : public OptimizerPass {
public:
    const char* name() const override { return "函数内联"; }
    bool run(TACProgram& program, int funcIdx) override;
};

class StrengthReduction : public OptimizerPass {
public:
    const char* name() const override { return "强度削减"; }
    bool run(TACProgram& program, int funcIdx) override;
};

class PassManager {
public:
    PassManager();
    bool runAll(TACProgram& program);

private:
    std::vector<std::unique_ptr<OptimizerPass>> passes;
    static const int MAX_ITERATIONS = 10;
};