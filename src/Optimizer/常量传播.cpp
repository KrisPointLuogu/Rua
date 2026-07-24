#include <algorithm>
#include <iostream>
#include "Optimizer/优化管理器.h"

bool ConstantPropagation::run(TACProgram& program, int funcIdx)
{
    if (funcIdx < 0 || funcIdx >= static_cast<int>(program.functions.size()))
        return false;

    auto& func = program.functions[funcIdx];
    bool changed = false;

    std::vector<int> constVals(256, 0);
    std::vector<bool> isConst(256, false);

    for (const auto& param : func.instructions) {
        // params are not constant
    }

    for (auto& inst : func.instructions) {
        auto op = inst->getOpcode();
        if (op == TACOpcode::MOVI) {
            auto* m = static_cast<TACMovI*>(inst.get());
            if (m->rd.kind == TACValueKind::TEMP) {
                if (!isConst[m->rd.index]
                    || constVals[m->rd.index] != m->constVal) {
                    constVals[m->rd.index] = m->constVal;
                    isConst[m->rd.index] = true;
                    changed = true;
                }
            }
        } else if (op == TACOpcode::MOV) {
            auto* m = static_cast<TACMov*>(inst.get());
            if (m->rs.kind == TACValueKind::VAR && isConst[m->rs.index]) {
                inst = std::make_unique<TACMovI>(m->rd, constVals[m->rs.index]);
                if (m->rd.kind == TACValueKind::TEMP) {
                    constVals[m->rd.index] = constVals[m->rs.index];
                    isConst[m->rd.index] = true;
                }
                changed = true;
            } else if (m->rs.kind == TACValueKind::TEMP
                       && isConst[m->rs.index]) {
                if (m->rd.kind == TACValueKind::TEMP) {
                    constVals[m->rd.index] = constVals[m->rs.index];
                    isConst[m->rd.index] = true;
                }
            }
        } else if (op == TACOpcode::ADD || op == TACOpcode::SUB
                   || op == TACOpcode::MUL || op == TACOpcode::DIV
                   || op == TACOpcode::MOD) {
            auto* b = static_cast<TACBinary*>(inst.get());
            bool lhsConst
                = (b->rs1.kind == TACValueKind::TEMP && isConst[b->rs1.index]);
            bool rhsConst
                = (b->rs2.kind == TACValueKind::TEMP && isConst[b->rs2.index]);
            if (lhsConst && rhsConst) {
                int l = constVals[b->rs1.index];
                int r = constVals[b->rs2.index];
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
                if (b->rd.kind == TACValueKind::TEMP) {
                    constVals[b->rd.index] = result;
                    isConst[b->rd.index] = true;
                }
                changed = true;
            }
        } else if (op == TACOpcode::EQ || op == TACOpcode::NE
                   || op == TACOpcode::LT || op == TACOpcode::GT
                   || op == TACOpcode::LE || op == TACOpcode::GE) {
            auto* b = static_cast<TACBinary*>(inst.get());
            bool lhsConst
                = (b->rs1.kind == TACValueKind::TEMP && isConst[b->rs1.index]);
            bool rhsConst
                = (b->rs2.kind == TACValueKind::TEMP && isConst[b->rs2.index]);
            if (lhsConst && rhsConst) {
                int l = constVals[b->rs1.index];
                int r = constVals[b->rs2.index];
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
                if (b->rd.kind == TACValueKind::TEMP) {
                    constVals[b->rd.index] = result;
                    isConst[b->rd.index] = true;
                }
                changed = true;
            }
        } else {
            if (op != TACOpcode::JMP && op != TACOpcode::JIF)
                isConst.assign(256, false);
        }
    }
    return changed;
}