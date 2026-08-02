#pragma once
#include "IR/TACInstructions.h"
#include <iostream>

#if defined(_DEBUG) && defined(OPTIMIZATION)

/*
 * TAC (IR) 调试输出 —— 仅在 _DEBUG 且启用 OPTIMIZATION 时可用。
 * 以线性汇编风格打印每个函数的三地址码，便于排查生成与优化结果。
 */
inline void dumpTACFunction(const TACFunction& func)
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
        } else if (op == TACOpcode::ARRNEW) {
            auto* n = static_cast<const TACArrayNew*>(inst.get());
            std::cout << "ARRNEW r" << n->rd.index << ", r" << n->size.index
                      << ", r" << n->init.index;
        } else if (op == TACOpcode::ARRGET) {
            auto* g = static_cast<const TACArrayGet*>(inst.get());
            std::cout << "ARRGET r" << g->rd.index << ", r" << g->arr.index
                      << ", r" << g->idx.index;
        } else if (op == TACOpcode::ARRSET) {
            auto* s = static_cast<const TACArraySet*>(inst.get());
            std::cout << "ARRSET r" << s->val.index << ", r" << s->arr.index
                      << ", r" << s->idx.index;
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

inline void dumpTACProgram(const TACProgram& program)
{
    std::cout << "\n========== TAC (IR) ==========\n";
    std::cout << "入口函数: " << program.entryPoint << "\n";

    std::cout << "--- 整数常量表 ---\n";
    for (size_t i = 0; i < program.constants.size(); i++)
        std::cout << "  [" << i << "] " << program.constants[i] << "\n";

    std::cout << "--- 字符串表 ---\n";
    for (size_t i = 0; i < program.strings.size(); i++)
        std::cout << "  [" << i << "] \"" << program.strings[i] << "\"\n";

    std::cout << "--- 函数 ---\n";
    for (size_t i = 0; i < program.functions.size(); i++) {
        const auto& f = program.functions[i];
        std::cout << "  [" << i << "] " << f.name << " (参数=" << f.paramCount
                  << ", 局部=" << f.localCount << ", 寄存器=" << f.regCount
                  << ")\n";
    }
    for (const auto& f : program.functions) dumpTACFunction(f);
    std::cout << "==============================\n";
}

#endif // _DEBUG && OPTIMIZATION
