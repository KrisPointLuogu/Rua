#include "Optimizer/优化管理器.h"
#include <iostream>

#ifdef _DEBUG
namespace {
void dumpTACFunction(const TACFunction& func)
{
    std::cout << "  Function " << func.name << " (" << func.instructions.size()
              << " insts):\n";
    for (int ti = 0; ti < static_cast<int>(func.instructions.size()); ti++) {
        auto& inst = func.instructions[ti];
        std::cout << "    " << ti << ": ";
        auto op = inst->getOpcode();
        if (op == TACOpcode::MOVI) {
            auto* m = static_cast<const TACMovI*>(inst.get());
            std::cout << "MOVI r" << m->rd.index << ", #" << m->constVal;
        } else if (op == TACOpcode::MOVS) {
            auto* m = static_cast<const TACMovS*>(inst.get());
            std::cout << "MOVS r" << m->rd.index << ", str" << m->stringIdx;
        } else if (op == TACOpcode::MOV) {
            auto* m = static_cast<const TACMov*>(inst.get());
            std::cout << "MOV r" << m->rd.index << ", r" << m->rs.index;
        } else if (op == TACOpcode::JMP) {
            auto* j = static_cast<const TACJmp*>(inst.get());
            std::cout << "JMP " << j->targetBlock;
        } else if (op == TACOpcode::JIF) {
            auto* j = static_cast<const TACJif*>(inst.get());
            std::cout << "JIF r" << j->cond.index << ", t=" << j->targetBlock
                      << " f=" << j->fallBlock;
        } else if (op == TACOpcode::PUSH) {
            auto* p = static_cast<const TACParm*>(inst.get());
            std::cout << "PUSH r" << p->rs.index;
        } else if (op == TACOpcode::CALL) {
            auto* c = static_cast<const TACCall*>(inst.get());
            std::cout << "CALL " << c->funcName << " -> r" << c->rd.index;
        } else if (op == TACOpcode::PRINT) {
            auto* p = static_cast<const TACPrint*>(inst.get());
            std::cout << "PRINT r" << p->rs.index;
        } else if (op == TACOpcode::RET) {
            auto* r = static_cast<const TACRet*>(inst.get());
            std::cout << "RET r" << r->rs.index;
        } else if (op == TACOpcode::HALT) {
            std::cout << "HALT";
        } else if (op == TACOpcode::NOP) {
            std::cout << "NOP";
        } else {
            auto* b = static_cast<const TACBinary*>(inst.get());
            const char* opname = "???";
            switch (op) {
            case TACOpcode::ADD: opname = "ADD"; break;
            case TACOpcode::SUB: opname = "SUB"; break;
            case TACOpcode::MUL: opname = "MUL"; break;
            case TACOpcode::DIV: opname = "DIV"; break;
            case TACOpcode::MOD: opname = "MOD"; break;
            case TACOpcode::EQ: opname = "EQ"; break;
            case TACOpcode::NE: opname = "NE"; break;
            case TACOpcode::LT: opname = "LT"; break;
            case TACOpcode::GT: opname = "GT"; break;
            case TACOpcode::LE: opname = "LE"; break;
            case TACOpcode::GE: opname = "GE"; break;
            default: break;
            }
            std::cout << opname << " r" << b->rd.index << ", r" << b->rs1.index
                      << ", r" << b->rs2.index;
        }
        std::cout << "\n";
    }
}
} // namespace
#endif

PassManager::PassManager()
{
    passes.push_back(std::make_unique<ConstantPropagation>());
    passes.push_back(std::make_unique<DeadCodeElimination>());
    // TODO: LICM, LoopUnrolling, FunctionInlining need jump target fixup
    // passes.push_back(std::make_unique<FunctionInlining>());
    // passes.push_back(std::make_unique<LoopInvariantCodeMotion>());
    // passes.push_back(std::make_unique<LoopUnrolling>());
    passes.push_back(std::make_unique<StrengthReduction>());
}

bool PassManager::runAll(TACProgram& program)
{
    bool changed = false;
    for (int iter = 0; iter < MAX_ITERATIONS; iter++) {
        bool iterChanged = false;
        for (auto& pass : passes) {
            for (int i = 0; i < static_cast<int>(program.functions.size());
                 i++) {
                if (pass->run(program, i)) {
                    iterChanged = true;
#ifdef _DEBUG
                    std::cout << "  [" << pass->name() << "] func " << i
                              << " changed:\n";
                    dumpTACFunction(program.functions[i]);
#endif
                }
            }
        }
        if (!iterChanged) break;
        changed = true;
        std::cout << "Optimization pass " << (iter + 1)
                  << " completed, changed\n";
    }
    return changed;
}