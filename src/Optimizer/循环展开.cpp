#include <iostream>
#include "Optimizer/优化管理器.h"

bool LoopUnrolling::run(TACProgram& program, int funcIdx)
{
    if (funcIdx < 0 || funcIdx >= static_cast<int>(program.functions.size()))
        return false;

    auto& func = program.functions[funcIdx];
    bool changed = false;

    // Collect all back-edges first (don't modify while scanning)
    struct LoopInfo {
        int jmpPos;    // position of the JMP instruction
        int bodyStart; // target of JMP (loop header / cond start)
    };
    std::vector<LoopInfo> loops;
    for (int i = 0; i < static_cast<int>(func.instructions.size()); i++) {
        auto* inst = func.instructions[i].get();
        if (inst->getOpcode() != TACOpcode::JMP) continue;
        auto* jmp = static_cast<TACJmp*>(inst);
        int target = jmp->targetBlock;
        if (target >= 0 && target < i) { loops.push_back({ i, target }); }
    }

    // Process loops from back to front so earlier insertions don't affect later
    // positions
    for (int li = static_cast<int>(loops.size()) - 1; li >= 0; li--) {
        auto& loop = loops[li];
        int loopBodyStart = loop.bodyStart;
        int loopEnd = loop.jmpPos;

        int bodyLen = loopEnd - loopBodyStart;
        if (bodyLen <= 0 || bodyLen > 20) continue;
        if (bodyLen > 8) continue;

        int oldSize = static_cast<int>(func.instructions.size());

        // Duplicate the loop body (excluding the JMP at the end)
        std::vector<std::unique_ptr<TACInst>> bodyCopy;
        for (int j = loopBodyStart; j < loopEnd; j++)
            bodyCopy.push_back(func.instructions[j]->clone());

        // Fix jump targets in cloned instructions
        for (auto& inst : bodyCopy) {
            auto op = inst->getOpcode();
            if (op == TACOpcode::JMP) {
                auto* j = static_cast<TACJmp*>(inst.get());
                if (j->targetBlock >= loopBodyStart && j->targetBlock < loopEnd)
                    j->targetBlock += bodyLen;
            } else if (op == TACOpcode::JIF) {
                auto* j = static_cast<TACJif*>(inst.get());
                if (j->targetBlock >= loopBodyStart && j->targetBlock < loopEnd)
                    j->targetBlock += bodyLen;
                if (j->fallBlock >= loopBodyStart && j->fallBlock < loopEnd)
                    j->fallBlock += bodyLen;
            }
        }

        // Insert duplicated body before the JMP
        func.instructions.insert(func.instructions.begin() + loopEnd,
                                 std::make_move_iterator(bodyCopy.begin()),
                                 std::make_move_iterator(bodyCopy.end()));

        // Build remapping: instructions at loopEnd..end shift right by bodyLen
        std::vector<int> oldToNew(oldSize);
        for (int i = 0; i < oldSize; i++) {
            if (i < loopEnd)
                oldToNew[i] = i;
            else
                oldToNew[i] = i + bodyLen;
        }

        fixJumpTargets(func, oldToNew);
        changed = true;
    }
    return changed;
}
