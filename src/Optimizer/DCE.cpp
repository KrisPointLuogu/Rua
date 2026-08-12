#include <algorithm>
#include <iostream>
#include <set>
#include "Optimizer/PassManager.h"

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
        } else if (op == TACOpcode::ARRNEW) {
            // 空壳：数组指令是黑盒，所有引用的临时寄存器都必须视为活跃，
            // 否则索引常量等会被误删
            auto* n = static_cast<TACArrayNew*>(inst.get());
            if (n->rd.kind == TACValueKind::TEMP)
                usedTemps.insert(n->rd.index);
            if (n->size.kind == TACValueKind::TEMP)
                usedTemps.insert(n->size.index);
            if (n->init.kind == TACValueKind::TEMP)
                usedTemps.insert(n->init.index);
        } else if (op == TACOpcode::ARRGET) {
            auto* g = static_cast<TACArrayGet*>(inst.get());
            if (g->rd.kind == TACValueKind::TEMP) usedTemps.insert(g->rd.index);
            if (g->arr.kind == TACValueKind::TEMP) usedTemps.insert(g->arr.index);
            if (g->idx.kind == TACValueKind::TEMP) usedTemps.insert(g->idx.index);
        } else if (op == TACOpcode::ARRSET) {
            auto* s = static_cast<TACArraySet*>(inst.get());
            if (s->val.kind == TACValueKind::TEMP) usedTemps.insert(s->val.index);
            if (s->arr.kind == TACValueKind::TEMP) usedTemps.insert(s->arr.index);
            if (s->idx.kind == TACValueKind::TEMP) usedTemps.insert(s->idx.index);
        } else if (op == TACOpcode::ARRDIMSET) {
            auto* d = static_cast<TACArrayDimSet*>(inst.get());
            if (d->arr.kind == TACValueKind::TEMP)
                usedTemps.insert(d->arr.index);
            if (d->val.kind == TACValueKind::TEMP)
                usedTemps.insert(d->val.index);
        } else if (op == TACOpcode::ARRGETN) {
            auto* g = static_cast<TACArrayGetN*>(inst.get());
            if (g->rd.kind == TACValueKind::TEMP) usedTemps.insert(g->rd.index);
            if (g->arr.kind == TACValueKind::TEMP) usedTemps.insert(g->arr.index);
        } else if (op == TACOpcode::ARRSETN) {
            auto* s = static_cast<TACArraySetN*>(inst.get());
            if (s->val.kind == TACValueKind::TEMP) usedTemps.insert(s->val.index);
            if (s->arr.kind == TACValueKind::TEMP) usedTemps.insert(s->arr.index);
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
