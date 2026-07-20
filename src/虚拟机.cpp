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

Value::Value() : type(ValueType::INTEGER), intVal(0) {}
Value::Value(int v) : type(ValueType::INTEGER), intVal(v) {}
Value::Value(const string &s) : type(ValueType::STRING), intVal(0), strVal(s) {}

void Value::print() const
{
    switch (type)
    {
    case ValueType::INTEGER:
        std::cout << intVal;
        break;
    case ValueType::STRING:
        std::cout << strVal;
        break;
    }
}

// ==================== VMError ====================

VMError::VMError(const string &message) : std::runtime_error(message) {}

// ==================== VM ====================

VM::VM() : program(nullptr), ip(0) {}

void VM::run(const BytecodeProgram &prog)
{
    stack.clear();
    callStack.clear();
    frameStack.clear();

    program = &prog;

    if (prog.code.empty())
        throw VMError("字节码为空");

    const auto &code = prog.code;
    const auto &constants = prog.constants;
    const auto &strings = prog.strings;
    const auto &functions = prog.functions;

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
    (static_cast<int>(code[ip]) | (static_cast<int>(code[ip + 1]) << 8) | \
     (static_cast<int>(code[ip + 2]) << 16) | (static_cast<int>(code[ip + 3]) << 24))

#define EXPECT_INT(v, opname) \
    if ((v).type != ValueType::INTEGER) { \
        std::ostringstream _oss; \
        _oss << "运行时错误（IP=" << ip << "）：运算符 " << (opname) \
             << " 要求整数操作数，但遇到字符串"; \
        throw VMError(_oss.str()); \
    }

#define NEXT() \
    do { uint8_t _opc = code[ip]; ip++; goto *dispatch[_opc]; } while (0)

    NEXT();

    // ======== opcode dispatch ========

    op_halt:
        goto op_halt_end;

    op_iconst: {
        int idx = READ_OPERAND();
        ip += 4;
        stack.push_back(Value(constants[idx]));
        NEXT();
    }

    op_sconst: {
        int idx = READ_OPERAND();
        ip += 4;
        stack.push_back(Value(strings[idx]));
        NEXT();
    }

    op_add: {
        Value b = std::move(stack.back()); stack.pop_back();
        Value a = std::move(stack.back()); stack.pop_back();
        if (a.type == ValueType::INTEGER && b.type == ValueType::INTEGER)
            stack.push_back(Value(a.intVal + b.intVal));
        else if (a.type == ValueType::STRING && b.type == ValueType::STRING)
            stack.push_back(Value(a.strVal + b.strVal));
        else
            throw VMError("运行时错误：ADD 操作数类型不匹配");
        NEXT();
    }

    op_sub: {
        Value b = std::move(stack.back()); stack.pop_back();
        Value a = std::move(stack.back()); stack.pop_back();
        EXPECT_INT(a, "SUB");
        EXPECT_INT(b, "SUB");
        stack.push_back(Value(a.intVal - b.intVal));
        NEXT();
    }

    op_mul: {
        Value b = std::move(stack.back()); stack.pop_back();
        Value a = std::move(stack.back()); stack.pop_back();
        EXPECT_INT(a, "MUL");
        EXPECT_INT(b, "MUL");
        stack.push_back(Value(a.intVal * b.intVal));
        NEXT();
    }

    op_div: {
        Value b = std::move(stack.back()); stack.pop_back();
        Value a = std::move(stack.back()); stack.pop_back();
        EXPECT_INT(a, "DIV");
        EXPECT_INT(b, "DIV");
        if (b.intVal == 0)
            throw VMError("运行时错误：除数为零");
        stack.push_back(Value(a.intVal / b.intVal));
        NEXT();
    }

    op_mod: {
        Value b = std::move(stack.back()); stack.pop_back();
        Value a = std::move(stack.back()); stack.pop_back();
        EXPECT_INT(a, "MOD");
        EXPECT_INT(b, "MOD");
        if (b.intVal == 0)
            throw VMError("运行时错误：除数为零");
        stack.push_back(Value(a.intVal % b.intVal));
        NEXT();
    }

    op_eq: {
        Value b = std::move(stack.back()); stack.pop_back();
        Value a = std::move(stack.back()); stack.pop_back();
        if (a.type != b.type)
            stack.push_back(Value(0));
        else if (a.type == ValueType::INTEGER)
            stack.push_back(Value(a.intVal == b.intVal ? 1 : 0));
        else
            stack.push_back(Value(a.strVal == b.strVal ? 1 : 0));
        NEXT();
    }

    op_neq: {
        Value b = std::move(stack.back()); stack.pop_back();
        Value a = std::move(stack.back()); stack.pop_back();
        if (a.type != b.type)
            stack.push_back(Value(1));
        else if (a.type == ValueType::INTEGER)
            stack.push_back(Value(a.intVal != b.intVal ? 1 : 0));
        else
            stack.push_back(Value(a.strVal != b.strVal ? 1 : 0));
        NEXT();
    }

    op_lt: {
        Value b = std::move(stack.back()); stack.pop_back();
        Value a = std::move(stack.back()); stack.pop_back();
        EXPECT_INT(a, "LT");
        EXPECT_INT(b, "LT");
        stack.push_back(Value(a.intVal < b.intVal ? 1 : 0));
        NEXT();
    }

    op_gt: {
        Value b = std::move(stack.back()); stack.pop_back();
        Value a = std::move(stack.back()); stack.pop_back();
        EXPECT_INT(a, "GT");
        EXPECT_INT(b, "GT");
        stack.push_back(Value(a.intVal > b.intVal ? 1 : 0));
        NEXT();
    }

    op_jmp: {
        int offset = READ_OPERAND();
        ip += 4 + offset;
        NEXT();
    }

    op_jif: {
        int offset = READ_OPERAND();
        Value cond = std::move(stack.back()); stack.pop_back();
        EXPECT_INT(cond, "JIF");
        ip += (cond.intVal == 0) ? (4 + offset) : 4;
        NEXT();
    }

    op_load: {
        int slot = READ_OPERAND();
        ip += 4;
        int base = frameStack.back();
        int idx = base + slot;
        stack.push_back(stack[idx]);
        NEXT();
    }

    op_store: {
        int slot = READ_OPERAND();
        ip += 4;
        int base = frameStack.back();
        int idx = base + slot;
        stack[idx] = std::move(stack.back()); stack.pop_back();
        NEXT();
    }

    op_call: {
        int funcIdx = READ_OPERAND();
        const FunctionInfo &func = functions[funcIdx];
        int frameBase = static_cast<int>(stack.size()) - func.paramCount;
        frameStack.push_back(frameBase);

        for (int i = 0; i < func.localCount; i++)
            stack.push_back(Value(0));

        callStack.push_back(ip + 4);
        ip = func.codeOffset;
        NEXT();
    }

    op_ret: {
        Value retVal = std::move(stack.back()); stack.pop_back();

        int frameBase = frameStack.back();
        frameStack.pop_back();

        stack.resize(frameBase);
        stack.push_back(std::move(retVal));

        ip = callStack.back();
        callStack.pop_back();
        NEXT();
    }

    op_print: {
        Value v = std::move(stack.back()); stack.pop_back();
        v.print();
        NEXT();
    }

    op_pop: {
        stack.pop_back();
        NEXT();
    }

op_halt_end:

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

int VM::getStackDepth() const
{
    return static_cast<int>(stack.size());
}

int VM::getCallDepth() const
{
    return static_cast<int>(callStack.size());
}

// ==================== 栈操作 ====================

Value VM::pop()
{
    if (stack.empty())
        throw VMError("运行时错误：栈为空，无法弹出");
    Value v = stack.back();
    stack.pop_back();
    return v;
}

void VM::push(const Value &val)
{
    stack.push_back(val);
}

Value VM::peek(int offset) const
{
    if (offset < 0 || offset >= static_cast<int>(stack.size()))
        throw VMError("运行时错误：栈索引越界");
    return stack[stack.size() - 1 - offset];
}


