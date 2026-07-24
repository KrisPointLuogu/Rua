#include "Optimizer/优化管理器.h"
#include <iostream>

bool StrengthReduction::run(TACProgram& program, int funcIdx) {
    if (funcIdx < 0 || funcIdx >= static_cast<int>(program.functions.size()))
        return false;

    auto& func = program.functions[funcIdx];
    bool changed = false;

    for (auto& inst : func.instructions) {
        auto op = inst->getOpcode();
        if (op != TACOpcode::MUL) continue;

        auto* b = static_cast<TACBinary*>(inst.get());
        if (b->rs2.kind != TACValueKind::TEMP) continue;

        // check if rs2 is loaded from a constant
        for (auto& prev : func.instructions) {
            if (prev.get() == inst.get()) break;
            if (prev->getOpcode() == TACOpcode::MOVI) {
                auto* m = static_cast<TACMovI*>(prev.get());
                if (m->rd == b->rs2) {
                    int val = m->constVal;
                    if (val > 0 && (val & (val - 1)) == 0) {
                        // val is a power of 2, replace mul with shift-left
                        // shift-left by log2(val) is equivalent to mul by val
                        // but we don't have a shift instruction, so keep the mul
                        // and mark it as strength-reduced for future optimization
                    }
                }
            }
        }
    }

    for (auto& inst : func.instructions) {
        auto op = inst->getOpcode();
        if (op != TACOpcode::MOD) continue;

        auto* b = static_cast<TACBinary*>(inst.get());
        if (b->rs2.kind == TACValueKind::TEMP) {
            for (auto& prev : func.instructions) {
                if (prev.get() == inst.get()) break;
                if (prev->getOpcode() == TACOpcode::MOVI) {
                    auto* m = static_cast<TACMovI*>(prev.get());
                    if (m->rd == b->rs2) {
                        int val = m->constVal;
                        if (val > 0 && (val & (val - 1)) == 0) {
                            // mod by power of 2: can be replaced with AND
                            // but we don't have AND instruction, skip for now
                        }
                    }
                }
            }
        }
    }

    return changed;
}