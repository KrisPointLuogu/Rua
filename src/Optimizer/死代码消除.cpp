#include <algorithm>
#include <iostream>
#include <set>
#include "Optimizer/优化管理器.h"

bool DeadCodeElimination::run(TACProgram& program, int funcIdx)
{
    if (funcIdx < 0 || funcIdx >= static_cast<int>(program.functions.size()))
        return false;

    auto& func = program.functions[funcIdx];
    bool changed = false;

    std::set<int> usedTemps;

    for (auto& inst : func.instructions) {
        auto op = inst->getOpcode();
        if (op == TACOpcode::MOV) {
            auto* m = static_cast<TACMov*>(inst.get());
            if (m->rs.kind == TACValueKind::TEMP) usedTemps.insert(m->rs.index);
        } else if (op == TACOpcode::MOVI) {
            // MOVI only has a dest, no source temp
        } else if (op == TACOpcode::MOVS) {
            // MOVS only has a dest
        } else if (op == TACOpcode::ADD || op == TACOpcode::SUB
                   || op == TACOpcode::MUL || op == TACOpcode::DIV
                   || op == TACOpcode::MOD || op == TACOpcode::EQ
                   || op == TACOpcode::NE || op == TACOpcode::LT
                   || op == TACOpcode::GT || op == TACOpcode::LE
                   || op == TACOpcode::GE) {
            auto* b = static_cast<TACBinary*>(inst.get());
            if (b->rs1.kind == TACValueKind::TEMP)
                usedTemps.insert(b->rs1.index);
            if (b->rs2.kind == TACValueKind::TEMP)
                usedTemps.insert(b->rs2.index);
        } else if (op == TACOpcode::JIF) {
            auto* j = static_cast<TACJif*>(inst.get());
            if (j->cond.kind == TACValueKind::TEMP)
                usedTemps.insert(j->cond.index);
        } else if (op == TACOpcode::PUSH) {
            auto* p = static_cast<TACParm*>(inst.get());
            if (p->rs.kind == TACValueKind::TEMP) usedTemps.insert(p->rs.index);
        } else if (op == TACOpcode::CALL) {
            auto* c = static_cast<TACCall*>(inst.get());
            if (c->rd.kind == TACValueKind::TEMP) usedTemps.insert(c->rd.index);
        } else if (op == TACOpcode::PRINT) {
            auto* p = static_cast<TACPrint*>(inst.get());
            if (p->rs.kind == TACValueKind::TEMP) usedTemps.insert(p->rs.index);
        } else if (op == TACOpcode::RET) {
            auto* r = static_cast<TACRet*>(inst.get());
            if (r->rs.kind == TACValueKind::TEMP) usedTemps.insert(r->rs.index);
        }
    }

    for (auto& inst : func.instructions) {
        auto op = inst->getOpcode();
        if (op == TACOpcode::MOV) {
            auto* m = static_cast<TACMov*>(inst.get());
            if (m->rd.kind == TACValueKind::TEMP
                && usedTemps.find(m->rd.index) == usedTemps.end()) {
                inst = std::make_unique<TACNop>();
                changed = true;
            }
        } else if (op == TACOpcode::ADD || op == TACOpcode::SUB
                   || op == TACOpcode::MUL || op == TACOpcode::DIV
                   || op == TACOpcode::MOD || op == TACOpcode::EQ
                   || op == TACOpcode::NE || op == TACOpcode::LT
                   || op == TACOpcode::GT || op == TACOpcode::LE
                   || op == TACOpcode::GE) {
            auto* b = static_cast<TACBinary*>(inst.get());
            if (b->rd.kind == TACValueKind::TEMP
                && usedTemps.find(b->rd.index) == usedTemps.end()) {
                inst = std::make_unique<TACNop>();
                changed = true;
            }
        } else if (op == TACOpcode::MOVI) {
            auto* m = static_cast<TACMovI*>(inst.get());
            if (m->rd.kind == TACValueKind::TEMP
                && usedTemps.find(m->rd.index) == usedTemps.end()) {
                inst = std::make_unique<TACNop>();
                changed = true;
            }
        }
    }

    return changed;
}
