#include <iostream>
#include "Optimizer/PassManager.h"

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
            auto op = inst->getOpcode();
            int target = -1;
            if (op == TACOpcode::JMP) {
                target = static_cast<TACJmp*>(inst)->targetBlock;
            } else if (op == TACOpcode::JIF) {
                target = static_cast<TACJif*>(inst)->targetBlock;
            }
            if (target >= 0 && target < i) {
                loopStarts.push_back(target);
                loopEnds.push_back(i);
            }
        }

        if (loopStarts.empty()) break;

        int lStart = loopStarts[0];
        int lEnd = loopEnds[0];

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
        std::vector<bool> loopDefs(256, false);
        for (int i = 0; i < 256; i++) loopDefs[i] = (loopWriteCount[i] > 0);

        std::vector<int> invariantInsts;
        for (int i = lStart; i <= lEnd; i++) {
            auto* inst = func.instructions[i].get();
            auto op = inst->getOpcode();

            bool isInvariant = false;
            if (op == TACOpcode::MOVI) {
                auto* m = static_cast<TACMovI*>(inst);
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
                if (b->rs1.kind == TACValueKind::TEMP && loopDefs[b->rs1.index])
                    operandsInvariant = false;
                if (b->rs2.kind == TACValueKind::TEMP && loopDefs[b->rs2.index])
                    operandsInvariant = false;
                if (operandsInvariant && b->rd.kind == TACValueKind::TEMP
                    && loopWriteCount[b->rd.index] != 1)
                    operandsInvariant = false;
                isInvariant = operandsInvariant;
            }

            if (isInvariant) invariantInsts.push_back(i);
        }

        if (invariantInsts.empty()) continue;

        int oldSize = static_cast<int>(func.instructions.size());

        std::vector<std::unique_ptr<TACInst>> hoisted;
        for (int i = static_cast<int>(invariantInsts.size()) - 1; i >= 0; i--) {
            int instIdx = invariantInsts[i];
            hoisted.push_back(std::move(func.instructions[instIdx]));
            func.instructions.erase(func.instructions.begin() + instIdx);
        }

        std::reverse(hoisted.begin(), hoisted.end());
        int insertCount = static_cast<int>(hoisted.size());
        func.instructions.insert(func.instructions.begin() + lStart,
                                 std::make_move_iterator(hoisted.begin()),
                                 std::make_move_iterator(hoisted.end()));

        std::vector<int> fresh(oldSize);
        std::vector<int> removedSorted = invariantInsts;
        std::sort(removedSorted.begin(), removedSorted.end());

        int remIdx = 0;
        int newPos = 0;
        for (int oldIdx = 0; oldIdx < oldSize; oldIdx++) {
            if (remIdx < static_cast<int>(removedSorted.size())
                && removedSorted[remIdx] == oldIdx) {
                fresh[oldIdx] = -1;
                remIdx++;
            } else {
                fresh[oldIdx] = newPos;
                newPos++;
            }
        }

        for (int i = 0; i < oldSize; i++) {
            if (fresh[i] >= 0 && fresh[i] >= lStart) {
                fresh[i] += insertCount;
            }
        }

        for (int k = 0; k < insertCount; k++) {
            fresh[removedSorted[k]] = lStart + k;
        }

        fixJumpTargets(func, fresh);
        changed = true;
        break;
    }
    return changed;
}
