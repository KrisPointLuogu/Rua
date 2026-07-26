#include <iostream>
#include <optional>
#include "Optimizer/PassManager.h"

static std::optional<int> findConstBefore(const TACFunction& func, int pos,
                                          TACValue target)
{
    if (target.kind != TACValueKind::TEMP) return std::nullopt;
    for (int i = pos - 1; i >= 0; i--) {
        auto& inst = func.instructions[i];
        if (inst->getOpcode() == TACOpcode::MOVI) {
            auto* m = static_cast<TACMovI*>(inst.get());
            if (m->rd == target) return m->constVal;
        }
        auto op = inst->getOpcode();
        if (op == TACOpcode::MOV) {
            auto* mv = static_cast<TACMov*>(inst.get());
            if (mv->rd == target) return std::nullopt;
        } else if (op == TACOpcode::ADD || op == TACOpcode::SUB
                   || op == TACOpcode::MUL || op == TACOpcode::DIV
                   || op == TACOpcode::MOD || op == TACOpcode::EQ
                   || op == TACOpcode::NE || op == TACOpcode::LT
                   || op == TACOpcode::GT || op == TACOpcode::LE
                   || op == TACOpcode::GE) {
            auto* b = static_cast<TACBinary*>(inst.get());
            if (b->rd == target) return std::nullopt;
        } else if (op == TACOpcode::CALL) {
            auto* c = static_cast<TACCall*>(inst.get());
            if (c->rd == target) return std::nullopt;
        }
    }
    return std::nullopt;
}

bool StrengthReduction::run(TACProgram& program, int funcIdx)
{
    if (funcIdx < 0 || funcIdx >= static_cast<int>(program.functions.size()))
        return false;

    auto& func = program.functions[funcIdx];
    bool changed = false;

    for (int i = 0; i < static_cast<int>(func.instructions.size()); i++) {
        auto& inst = func.instructions[i];
        auto op = inst->getOpcode();

        if (op == TACOpcode::MUL || op == TACOpcode::DIV || op == TACOpcode::MOD
            || op == TACOpcode::ADD || op == TACOpcode::SUB) {
            auto* b = static_cast<TACBinary*>(inst.get());

            auto c2 = findConstBefore(func, i, b->rs2);
            std::optional<int> c1;
            if (op == TACOpcode::ADD || op == TACOpcode::MUL)
                c1 = findConstBefore(func, i, b->rs1);

            int constVal = 0;
            TACValue otherReg = b->rs1;
            if (c2.has_value()) {
                constVal = *c2;
                otherReg = b->rs1;
            } else if (c1.has_value()) {
                constVal = *c1;
                otherReg = b->rs2;
            } else {
                continue;
            }

            switch (op) {
            case TACOpcode::ADD:
                if (constVal == 0) {
                    inst = std::make_unique<TACMov>(b->rd, otherReg);
                    changed = true;
                }
                break;
            case TACOpcode::SUB:
                if (c2.has_value() && constVal == 0) {
                    inst = std::make_unique<TACMov>(b->rd, otherReg);
                    changed = true;
                }
                break;
            case TACOpcode::MUL:
                if (constVal == 0) {
                    inst = std::make_unique<TACMovI>(b->rd, 0);
                    changed = true;
                } else if (constVal == 1) {
                    inst = std::make_unique<TACMov>(b->rd, otherReg);
                    changed = true;
                }
                break;
            case TACOpcode::DIV:
                if (constVal == 1) {
                    inst = std::make_unique<TACMov>(b->rd, otherReg);
                    changed = true;
                }
                break;
            case TACOpcode::MOD:
                if (constVal == 1) {
                    inst = std::make_unique<TACMovI>(b->rd, 0);
                    changed = true;
                }
                break;
            default: break;
            }
        }
    }

    return changed;
}
