/*
 * 栈式虚拟机 —— VM
 *
 * 执行由编译器生成的 Rua 字节码。
 *
 * 架构：
 *   值栈（stack）        — 运算和参数传递
 *   调用栈（callStack）  — 保存返回地址
 *   帧栈（frameStack）   — 每个函数栈帧的基址
 *   IP（ip）             — 指令指针
 */

#include "虚拟机.h"
#include <iostream>
#include <sstream>
#include <cassert>
#ifdef _DEBUG
#include <chrono>
#endif

using std::string;
using std::vector;

// ==================== Value ====================

void Value::print() const
{
    if (type == ValueType::INTEGER)
        std::cout << data;
    else
        std::cout << "(string)";
}

// ==================== VMError ====================

VMError::VMError(const string &message) : std::runtime_error(message) {}

// ==================== VM ====================

VM::VM()
    : stackData(std::make_unique<Value[]>(STACK_CAP))
    , callStackData(std::make_unique<int[]>(CALL_CAP))
    , frameStackData(std::make_unique<int[]>(FRAME_CAP))
    , sp(-1), cp(-1), fp(-1), program(nullptr), ip(0)
{
}

void VM::run(const BytecodeProgram &prog)
{
    program = &prog;

    if (prog.code.empty())
        throw VMError("字节码为空");

    const auto &code = prog.code;
    const auto &constants = prog.constants;
    const auto &strings = prog.strings;
    const auto &functions = prog.functions;

    Value* const s = stackData.get();
    int* const cs = callStackData.get();
    int* const fs = frameStackData.get();

    int _sp = -1;
    int _cp = -1;
    int _fp = -1;

    vector<string> strPool;

#ifdef _DEBUG
    auto 开始 = std::chrono::high_resolution_clock::now();
#endif

    static void *dispatch[] = {
        &&op_halt, &&op_iconst, &&op_sconst, &&op_add,
        &&op_sub, &&op_mul, &&op_div, &&op_eq,
        &&op_jmp, &&op_jif, &&op_load, &&op_store,
        &&op_call, &&op_print, &&op_ret, &&op_pop,
        &&op_lt, &&op_gt, &&op_neq, &&op_mod,
    };

    ip = 0;

#define READ_OPERAND() \
    ({ int _v; __builtin_memcpy(&_v, &code[ip], 4); _v; })

#ifdef _DEBUG
#define EXPECT_INT(v, opname) \
    if ((v).type != ValueType::INTEGER) { \
        std::ostringstream _oss; \
        _oss << "运行时错误（IP=" << ip << "）：运算符 " << (opname) \
             << " 要求整数操作数，但遇到字符串"; \
        throw VMError(_oss.str()); \
    }
#else
#define EXPECT_INT(v, opname) ((void)0)
#endif

#define NEXT() \
    do { uint8_t _opc = code[ip]; ip++; goto *dispatch[_opc]; } while (0)

    NEXT();

    // ======== opcode dispatch ========

    op_halt:
        goto op_halt_end;

    op_iconst: {
        const int idx = READ_OPERAND();
        ++_sp;
        s[_sp].type = ValueType::INTEGER;
        s[_sp].data = constants[idx];
        ip += 4;
        NEXT();
    }

    op_sconst: {
        const int idx = READ_OPERAND();
        strPool.push_back(strings[idx]);
        ++_sp;
        s[_sp].type = ValueType::STRING;
        s[_sp].data = static_cast<int>(strPool.size()) - 1;
        ip += 4;
        NEXT();
    }

    op_add: {
        Value b = s[_sp--];
        Value a = s[_sp--];
        if (a.type == ValueType::INTEGER && b.type == ValueType::INTEGER) {
            ++_sp;
            s[_sp].type = ValueType::INTEGER;
            s[_sp].data = a.data + b.data;
        } else if (a.type == ValueType::STRING && b.type == ValueType::STRING) {
            strPool.push_back(strPool[a.data] + strPool[b.data]);
            ++_sp;
            s[_sp].type = ValueType::STRING;
            s[_sp].data = static_cast<int>(strPool.size()) - 1;
        } else
            throw VMError("运行时错误：ADD 操作数类型不匹配");
        NEXT();
    }

    op_sub: {
        Value b = s[_sp--];
        Value a = s[_sp--];
        EXPECT_INT(a, "SUB");
        EXPECT_INT(b, "SUB");
        ++_sp;
        s[_sp].type = ValueType::INTEGER;
        s[_sp].data = a.data - b.data;
        NEXT();
    }

    op_mul: {
        Value b = s[_sp--];
        Value a = s[_sp--];
        EXPECT_INT(a, "MUL");
        EXPECT_INT(b, "MUL");
        ++_sp;
        s[_sp].type = ValueType::INTEGER;
        s[_sp].data = a.data * b.data;
        NEXT();
    }

    op_div: {
        Value b = s[_sp--];
        Value a = s[_sp--];
        EXPECT_INT(a, "DIV");
        EXPECT_INT(b, "DIV");
        if (b.data == 0)
            throw VMError("运行时错误：除数为零");
        ++_sp;
        s[_sp].type = ValueType::INTEGER;
        s[_sp].data = a.data / b.data;
        NEXT();
    }

    op_mod: {
        Value b = s[_sp--];
        Value a = s[_sp--];
        EXPECT_INT(a, "MOD");
        EXPECT_INT(b, "MOD");
        if (b.data == 0)
            throw VMError("运行时错误：除数为零");
        ++_sp;
        s[_sp].type = ValueType::INTEGER;
        s[_sp].data = a.data % b.data;
        NEXT();
    }

    op_eq: {
        Value b = s[_sp--];
        Value a = s[_sp--];
        ++_sp;
        s[_sp].type = ValueType::INTEGER;
        if (a.type != b.type)
            s[_sp].data = 0;
        else if (a.type == ValueType::INTEGER)
            s[_sp].data = a.data == b.data ? 1 : 0;
        else
            s[_sp].data = strPool[a.data] == strPool[b.data] ? 1 : 0;
        NEXT();
    }

    op_neq: {
        Value b = s[_sp--];
        Value a = s[_sp--];
        ++_sp;
        s[_sp].type = ValueType::INTEGER;
        if (a.type != b.type)
            s[_sp].data = 1;
        else if (a.type == ValueType::INTEGER)
            s[_sp].data = a.data != b.data ? 1 : 0;
        else
            s[_sp].data = strPool[a.data] != strPool[b.data] ? 1 : 0;
        NEXT();
    }

    op_lt: {
        Value b = s[_sp--];
        Value a = s[_sp--];
        EXPECT_INT(a, "LT");
        EXPECT_INT(b, "LT");
        ++_sp;
        s[_sp].type = ValueType::INTEGER;
        s[_sp].data = a.data < b.data ? 1 : 0;
        NEXT();
    }

    op_gt: {
        Value b = s[_sp--];
        Value a = s[_sp--];
        EXPECT_INT(a, "GT");
        EXPECT_INT(b, "GT");
        ++_sp;
        s[_sp].type = ValueType::INTEGER;
        s[_sp].data = a.data > b.data ? 1 : 0;
        NEXT();
    }

    op_jmp: {
        const int offset = READ_OPERAND();
        ip += 4 + offset;
        NEXT();
    }

    op_jif: {
        const int offset = READ_OPERAND();
        Value cond = s[_sp--];
        EXPECT_INT(cond, "JIF");
        ip += (cond.data == 0) ? (4 + offset) : 4;
        NEXT();
    }

    op_load: {
        const int slot = READ_OPERAND();
        ip += 4;
        const int base = fs[_fp];
        s[++_sp] = s[base + slot];
        NEXT();
    }

    op_store: {
        const int slot = READ_OPERAND();
        ip += 4;
        const int base = fs[_fp];
        s[base + slot] = s[_sp--];
        NEXT();
    }

    op_call: {
        const int funcIdx = READ_OPERAND();
        const FunctionInfo &func = functions[funcIdx];
        const int frameBase = _sp + 1 - func.paramCount;
        fs[++_fp] = frameBase;

        for (int i = 0; i < func.localCount; i++) {
            ++_sp;
            s[_sp].type = ValueType::INTEGER;
            s[_sp].data = 0;
        }

        cs[++_cp] = ip + 4;
        ip = func.codeOffset;
        NEXT();
    }

    op_ret: {
        Value retVal = s[_sp--];

        const int frameBase = fs[_fp--];

        _sp = frameBase - 1;
        s[++_sp] = retVal;

        ip = cs[_cp--];
        NEXT();
    }

    op_print: {
        Value v = s[_sp--];
        if (v.type == ValueType::INTEGER)
            std::cout << v.data;
        else
            std::cout << strPool[v.data];
        NEXT();
    }

    op_pop: {
        _sp--;
        NEXT();
    }

op_halt_end:
    sp = _sp;
    cp = _cp;
    fp = _fp;

#ifdef _DEBUG
    {
        auto 结束 = std::chrono::high_resolution_clock::now();
        auto 耗时 = std::chrono::duration_cast<std::chrono::microseconds>(结束 - 开始).count();
        std::cout << "\n[DEBUG] 字节码执行耗时: " << 耗时 << " 微秒 (" << (耗时 / 1000.0) << " 毫秒)\n";
    }
#endif
}

#undef READ_OPERAND
#undef EXPECT_INT
#undef NEXT
