#include <algorithm>
#include <iostream>
#include <unordered_map>
#include <vector>
#include "Optimizer/优化管理器.h"

bool ConstantPropagation::run(TACProgram& program, int funcIdx)
{
    if (funcIdx < 0 || funcIdx >= static_cast<int>(program.functions.size()))
        return false;

    auto& func = program.functions[funcIdx];
    bool changed = false;

    // Detect loop body ranges via backward jumps.
    // Any instruction in [target, jumpIdx] is inside a loop body.
    struct LoopRange { int start, end; };
    std::vector<LoopRange> loops;
    for (int i = 0; i < static_cast<int>(func.instructions.size()); i++) {
        auto op = func.instructions[i]->getOpcode();
        int target = -1;
        if (op == TACOpcode::JMP) {
            target = static_cast<TACJmp*>(func.instructions[i].get())->targetBlock;
        } else if (op == TACOpcode::JIF) {
            target = static_cast<TACJif*>(func.instructions[i].get())->targetBlock;
        }
        if (target >= 0 && target < i) {
            loops.push_back({target, i});
        }
    }

    auto isInLoop = [&](int idx) -> bool {
        for (auto& lr : loops) {
            if (idx >= lr.start && idx <= lr.end) return true;
        }
        return false;
    };

    // Use a single map keyed by register index (ignoring VAR/TEMP distinction)
    // because VAR[X] and TEMP[X] share the same physical register.
    std::unordered_map<int, int> regConstVals;
    std::unordered_map<int, bool> regIsConst;

    auto clearConst = [&](int regIdx) {
        regIsConst[regIdx] = false;
    };

    auto setConst = [&](int regIdx, int val) {
        regConstVals[regIdx] = val;
        regIsConst[regIdx] = true;
    };

    auto isConst = [&](int regIdx) -> bool {
        auto it = regIsConst.find(regIdx);
        return it != regIsConst.end() && it->second;
    };

    auto getConst = [&](int regIdx) -> int {
        return regConstVals[regIdx];
    };

    for (int i = 0; i < static_cast<int>(func.instructions.size()); i++) {
        auto& inst = func.instructions[i];
        auto op = inst->getOpcode();

        if (op == TACOpcode::MOVI) {
            auto* m = static_cast<TACMovI*>(inst.get());
            setConst(m->rd.index, m->constVal);
        } else if (op == TACOpcode::MOVS) {
            auto* m = static_cast<TACMovS*>(inst.get());
            clearConst(m->rd.index);
        } else if (op == TACOpcode::MOV) {
            auto* m = static_cast<TACMov*>(inst.get());
            if (isConst(m->rs.index)) {
                setConst(m->rd.index, getConst(m->rs.index));
            } else {
                clearConst(m->rd.index);
            }
        } else if (op == TACOpcode::ADD || op == TACOpcode::SUB
                   || op == TACOpcode::MUL || op == TACOpcode::DIV
                   || op == TACOpcode::MOD) {
            auto* b = static_cast<TACBinary*>(inst.get());
            bool lhsConst = isConst(b->rs1.index);
            bool rhsConst = isConst(b->rs2.index);
            if (lhsConst && rhsConst && !isInLoop(i)) {
                int l = getConst(b->rs1.index);
                int r = getConst(b->rs2.index);
                int result = 0;
                switch (op) {
                case TACOpcode::ADD: result = l + r; break;
                case TACOpcode::SUB: result = l - r; break;
                case TACOpcode::MUL: result = l * r; break;
                case TACOpcode::DIV: result = (r != 0) ? l / r : 0; break;
                case TACOpcode::MOD: result = (r != 0) ? l % r : 0; break;
                default: break;
                }
                inst = std::make_unique<TACMovI>(b->rd, result);
                setConst(b->rd.index, result);
                changed = true;
            } else {
                clearConst(b->rd.index);
            }
        } else if (op == TACOpcode::EQ || op == TACOpcode::NE
                   || op == TACOpcode::LT || op == TACOpcode::GT
                   || op == TACOpcode::LE || op == TACOpcode::GE) {
            auto* b = static_cast<TACBinary*>(inst.get());
            bool lhsConst = isConst(b->rs1.index);
            bool rhsConst = isConst(b->rs2.index);
            if (lhsConst && rhsConst && !isInLoop(i)) {
                int l = getConst(b->rs1.index);
                int r = getConst(b->rs2.index);
                int result = 0;
                switch (op) {
                case TACOpcode::EQ: result = (l == r) ? 1 : 0; break;
                case TACOpcode::NE: result = (l != r) ? 1 : 0; break;
                case TACOpcode::LT: result = (l < r) ? 1 : 0; break;
                case TACOpcode::GT: result = (l > r) ? 1 : 0; break;
                case TACOpcode::LE: result = (l <= r) ? 1 : 0; break;
                case TACOpcode::GE: result = (l >= r) ? 1 : 0; break;
                default: break;
                }
                inst = std::make_unique<TACMovI>(b->rd, result);
                setConst(b->rd.index, result);
                changed = true;
            } else {
                clearConst(b->rd.index);
            }
        } else if (op == TACOpcode::CALL) {
            auto* c = static_cast<TACCall*>(inst.get());
            clearConst(c->rd.index);
        }
    }
    return changed;
}
