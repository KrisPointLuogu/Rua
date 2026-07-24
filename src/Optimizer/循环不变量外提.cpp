#include <iostream>
#include "Optimizer/优化管理器.h"

bool LoopInvariantCodeMotion::run(TACProgram& program, int funcIdx)
{
    if (funcIdx < 0 || funcIdx >= static_cast<int>(program.functions.size()))
        return false;

    auto& func = program.functions[funcIdx];
    bool changed = false;

    for (int iter = 0; iter < 3; iter++) {
        std::vector<int> loopStarts;
        std::vector<int> loopEnds;

        for (int i = 0; i < static_cast<int>(func.instructions.size()); i++) {
            auto* inst = func.instructions[i].get();
            if (inst->getOpcode() == TACOpcode::JMP) {
                auto* jmp = static_cast<TACJmp*>(inst);
                int target = jmp->targetBlock;
                if (target >= 0 && target < i) {
                    loopStarts.push_back(target);
                    loopEnds.push_back(i);
                }
            }
        }

        if (loopStarts.empty()) break;

        for (size_t l = 0; l < loopStarts.size(); l++) {
            int lStart = loopStarts[l];
            int lEnd = loopEnds[l];

            // Count how many times each register is written in the loop
            std::vector<int> loopWriteCount(256, 0);
            for (int i = lStart; i <= lEnd; i++) {
                auto* inst = func.instructions[i].get();
                auto op = inst->getOpcode();
                if (op == TACOpcode::MOV) {
                    auto* m = static_cast<TACMov*>(inst);
                    if (m->rd.kind == TACValueKind::TEMP)
                        loopWriteCount[m->rd.index]++;
                } else if (op == TACOpcode::ADD || op == TACOpcode::SUB
                           || op == TACOpcode::MUL || op == TACOpcode::DIV
                           || op == TACOpcode::MOD || op == TACOpcode::EQ
                           || op == TACOpcode::NE || op == TACOpcode::LT
                           || op == TACOpcode::GT || op == TACOpcode::LE
                           || op == TACOpcode::GE) {
                    auto* b = static_cast<TACBinary*>(inst);
                    if (b->rd.kind == TACValueKind::TEMP)
                        loopWriteCount[b->rd.index]++;
                } else if (op == TACOpcode::CALL) {
                    auto* c = static_cast<TACCall*>(inst);
                    if (c->rd.kind == TACValueKind::TEMP)
                        loopWriteCount[c->rd.index]++;
                }
            }
            // loopDefs[i] is true if register i is written by ANY instruction
            // in the loop
            std::vector<bool> loopDefs(256, false);
            for (int i = 0; i < 256; i++) loopDefs[i] = (loopWriteCount[i] > 0);

            std::vector<int> invariantInsts;
            for (int i = lStart; i <= lEnd; i++) {
                auto* inst = func.instructions[i].get();
                auto op = inst->getOpcode();

                bool isInvariant = false;
                if (op == TACOpcode::MOVI) {
                    auto* m = static_cast<TACMovI*>(inst);
                    // Only hoist MOVI if its target register is not overwritten
                    // by other instructions in the loop (write count must be
                    // exactly 1, meaning only this MOVI writes to it)
                    if (m->rd.kind == TACValueKind::TEMP
                        && loopWriteCount[m->rd.index] == 1)
                        isInvariant = true;
                } else if (op == TACOpcode::ADD || op == TACOpcode::SUB
                           || op == TACOpcode::MUL || op == TACOpcode::DIV
                           || op == TACOpcode::MOD || op == TACOpcode::EQ
                           || op == TACOpcode::NE || op == TACOpcode::LT
                           || op == TACOpcode::GT || op == TACOpcode::LE
                           || op == TACOpcode::GE) {
                    auto* b = static_cast<TACBinary*>(inst);
                    bool operandsInvariant = true;
                    if (b->rs1.kind == TACValueKind::TEMP
                        && loopDefs[b->rs1.index])
                        operandsInvariant = false;
                    if (b->rs2.kind == TACValueKind::TEMP
                        && loopDefs[b->rs2.index])
                        operandsInvariant = false;
                    isInvariant = operandsInvariant;
                }

                if (isInvariant) invariantInsts.push_back(i);
            }

            if (invariantInsts.empty()) continue;

            int oldSize = static_cast<int>(func.instructions.size());

            // Collect the invariant instructions (from back to front to
            // preserve indices)
            std::vector<std::unique_ptr<TACInst>> hoisted;
            for (int i = static_cast<int>(invariantInsts.size()) - 1; i >= 0;
                 i--) {
                int instIdx = invariantInsts[i];
                hoisted.push_back(std::move(func.instructions[instIdx]));
                func.instructions.erase(func.instructions.begin() + instIdx);
            }

            // Reverse hoisted so they're in original order, then insert at
            // lStart
            std::reverse(hoisted.begin(), hoisted.end());
            int insertCount = static_cast<int>(hoisted.size());
            func.instructions.insert(func.instructions.begin() + lStart,
                                     std::make_move_iterator(hoisted.begin()),
                                     std::make_move_iterator(hoisted.end()));

            // Build remapping: invariant instructions moved from their original
            // positions to lStart..lStart+insertCount-1 Everything else shifted
            // accordingly
            std::vector<int> oldToNew(oldSize);
            // Start with identity
            for (int i = 0; i < oldSize; i++) oldToNew[i] = i;

            // Mark removed positions as -1
            for (int idx : invariantInsts) oldToNew[idx] = -1;

            // Compute cumulative shift for each position
            // After removing invariantInsts and inserting at lStart:
            // - Positions before lStart: unchanged (if not removed)
            // - Positions at lStart..lStart+insertCount-1: the hoisted
            // instructions
            // - Everything else shifts
            // Build a fresh mapping by tracking cumulative deletions and
            // insertions
            std::vector<int> fresh(oldSize);
            // Create a sorted list of removed positions
            std::vector<int> removedSorted = invariantInsts;
            std::sort(removedSorted.begin(), removedSorted.end());

            int remIdx = 0; // index into removedSorted
            int newPos = 0;
            for (int oldIdx = 0; oldIdx < oldSize; oldIdx++) {
                // Check if this position was removed
                if (remIdx < static_cast<int>(removedSorted.size())
                    && removedSorted[remIdx] == oldIdx) {
                    fresh[oldIdx] = -1;
                    remIdx++;
                } else {
                    fresh[oldIdx] = newPos;
                    newPos++;
                }
            }

            // Now account for the insertions at lStart
            // The hoisted instructions are inserted at lStart, shifting
            // everything at/after lStart right by insertCount
            for (int i = 0; i < oldSize; i++) {
                if (fresh[i] >= 0 && fresh[i] >= lStart) {
                    fresh[i] += insertCount;
                }
            }

            // The hoisted instructions map to lStart..lStart+insertCount-1
            for (int k = 0; k < insertCount; k++) {
                fresh[removedSorted[k]] = lStart + k;
            }

            fixJumpTargets(func, fresh);
            changed = true;
        }
    }
    return changed;
}
