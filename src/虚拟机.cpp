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
    : stackData(std::make_unique<Value[]>(STACK_CAP)), callStackData(std::make_unique<int[]>(CALL_CAP)), frameStackData(std::make_unique<int[]>(FRAME_CAP)), sp(-1), cp(-1), fp(-1), program(nullptr), ip(0)
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

    Value *const s = stackData.get();
    int *const cs = callStackData.get();
    int *const fs = frameStackData.get();

    int _sp = -1;
    int _cp = -1;
    int _fp = -1;
    Value _tos{0};

    vector<string> strPool;

    auto pushInt = [&](int data)
    { ++_sp; _tos = Value(data); s[_sp] = _tos; };
    auto pushStr = [&](int idx)
    { ++_sp; _tos = Value(idx, ValueType::STRING); s[_sp] = _tos; };
    auto push = [&](const Value &val)
    { s[++_sp] = val; _tos = val; };
    auto pop = [&]() -> Value
    { Value r = _tos; --_sp; if (_sp >= 0) _tos = s[_sp]; return r; };
    auto popInt = [&]() -> int
    { int r = _tos.data; --_sp; if (_sp >= 0) _tos = s[_sp]; return r; };
    auto pushCall = [&](int val)
    { cs[++_cp] = val; };
    auto popCall = [&]() -> int
    { return cs[_cp--]; };
    auto pushFrame = [&](int val)
    { fs[++_fp] = val; };
    auto popFrame = [&]() -> int
    { return fs[_fp--]; };
    auto peekFrame = [&]() -> int
    { return fs[_fp]; };

#ifdef _DEBUG
    auto 开始 = std::chrono::high_resolution_clock::now();
#endif

#ifdef OPTIMIZATION
    // 直接线程分发：goto *label 代替 switch
    static void *dispatch[] = {
        &&op_halt,
        &&op_iconst,
        &&op_sconst,
        &&op_add,
        &&op_sub,
        &&op_mul,
        &&op_div,
        &&op_eq,
        &&op_jmp,
        &&op_jif,
        &&op_load,
        &&op_store,
        &&op_call,
        &&op_print,
        &&op_ret,
        &&op_pop,
        &&op_lt,
        &&op_gt,
        &&op_neq,
        &&op_mod,
        &&op_sub_iconst,
        &&op_gt_iconst,
    };
#else
    // 朴素 switch 分发（无 OPTIMIZATION 时）
#endif

    ip = 0;

#define READ_OPERAND() \
    ({ int _v; __builtin_memcpy(&_v, &code[ip], 4); _v; })

#ifdef _DEBUG
#define EXPECT_INT(v, opname)                                        \
    if ((v).type != ValueType::INTEGER)                              \
    {                                                                \
        std::ostringstream _oss;                                     \
        _oss << "运行时错误（IP=" << ip << "）：运算符 " << (opname) \
             << " 要求整数操作数，但遇到字符串";                     \
        throw VMError(_oss.str());                                   \
    }
#else
#define EXPECT_INT(v, opname) ((void)0)
#endif

#ifdef OPTIMIZATION
#define NEXT()                   \
    do                           \
    {                            \
        uint8_t _opc = code[ip]; \
        ip++;                    \
        goto *dispatch[_opc];    \
    } while (0)

    NEXT();
#else
#define NEXT() goto op_next
op_next:
    while (ip < static_cast<int>(code.size()))
    {
        Opcode op = static_cast<Opcode>(code[ip]);
        ip++;
        if (op == Opcode::HALT)
            break;
        switch (op)
        {
        case Opcode::ICONST:
            goto op_iconst;
        case Opcode::SCONST:
            goto op_sconst;
        case Opcode::ADD:
            goto op_add;
        case Opcode::SUB:
            goto op_sub;
        case Opcode::MUL:
            goto op_mul;
        case Opcode::DIV:
            goto op_div;
        case Opcode::MOD:
            goto op_mod;
        case Opcode::EQ:
            goto op_eq;
        case Opcode::NEQ:
            goto op_neq;
        case Opcode::LT:
            goto op_lt;
        case Opcode::GT:
            goto op_gt;
        case Opcode::JMP:
            goto op_jmp;
        case Opcode::JIF:
            goto op_jif;
        case Opcode::LOAD:
            goto op_load;
        case Opcode::STORE:
            goto op_store;
        case Opcode::CALL:
            goto op_call;
        case Opcode::PRINT:
            goto op_print;
        case Opcode::RET:
            goto op_ret;
        case Opcode::POP:
            goto op_pop;
        default:
        {
            std::ostringstream oss;
            oss << "运行时错误：未知操作码 0x"
                << std::hex << static_cast<int>(op) << std::dec
                << "，IP=" << (ip - 1);
            throw VMError(oss.str());
        }
        }
    }
    goto op_halt_end;
#endif

    // ======== opcode dispatch ========

op_halt:
    goto op_halt_end;

op_iconst:
{
    const int idx = READ_OPERAND();
    pushInt(constants[idx]);
    ip += 4;
    NEXT();
}

op_sconst:
{
    const int idx = READ_OPERAND();
    strPool.push_back(strings[idx]);
    pushStr(static_cast<int>(strPool.size()) - 1);
    ip += 4;
    NEXT();
}

op_add:
{
    Value b = pop();
    Value a = pop();
    if (a.type == ValueType::INTEGER && b.type == ValueType::INTEGER)
        pushInt(a.data + b.data);
    else if (a.type == ValueType::STRING && b.type == ValueType::STRING)
    {
        strPool.push_back(strPool[a.data] + strPool[b.data]);
        pushStr(static_cast<int>(strPool.size()) - 1);
    }
    else
        throw VMError("运行时错误：ADD 操作数类型不匹配");
    NEXT();
}

op_sub:
{
    Value b = pop();
    Value a = pop();
    EXPECT_INT(a, "SUB");
    EXPECT_INT(b, "SUB");
    pushInt(a.data - b.data);
    NEXT();
}

#ifdef OPTIMIZATION
// 超级指令：SUB_ICONST ci — 取代 ICONST ci + SUB 两次分发
op_sub_iconst:
{
    const int ci = READ_OPERAND();
    EXPECT_INT(_tos, "SUB");
    _tos.data -= constants[ci];
    s[_sp] = _tos;
    ip += 4;
    NEXT();
}
#endif

op_mul:
{
    Value b = pop();
    Value a = pop();
    EXPECT_INT(a, "MUL");
    EXPECT_INT(b, "MUL");
    pushInt(a.data * b.data);
    NEXT();
}

op_div:
{
    Value b = pop();
    Value a = pop();
    EXPECT_INT(a, "DIV");
    EXPECT_INT(b, "DIV");
    if (b.data == 0)
        throw VMError("运行时错误：除数为零");
    pushInt(a.data / b.data);
    NEXT();
}

op_mod:
{
    Value b = pop();
    Value a = pop();
    EXPECT_INT(a, "MOD");
    EXPECT_INT(b, "MOD");
    if (b.data == 0)
        throw VMError("运行时错误：除数为零");
    pushInt(a.data % b.data);
    NEXT();
}

op_eq:
{
    Value b = pop();
    Value a = pop();
    int result;
    if (a.type != b.type)
        result = 0;
    else if (a.type == ValueType::INTEGER)
        result = a.data == b.data ? 1 : 0;
    else
        result = strPool[a.data] == strPool[b.data] ? 1 : 0;
    pushInt(result);
    NEXT();
}

op_neq:
{
    Value b = pop();
    Value a = pop();
    int result;
    if (a.type != b.type)
        result = 1;
    else if (a.type == ValueType::INTEGER)
        result = a.data != b.data ? 1 : 0;
    else
        result = strPool[a.data] != strPool[b.data] ? 1 : 0;
    pushInt(result);
    NEXT();
}

op_lt:
{
    Value b = pop();
    Value a = pop();
    EXPECT_INT(a, "LT");
    EXPECT_INT(b, "LT");
    pushInt(a.data < b.data ? 1 : 0);
    NEXT();
}

op_gt:
{
    Value b = pop();
    Value a = pop();
    EXPECT_INT(a, "GT");
    EXPECT_INT(b, "GT");
    pushInt(a.data > b.data ? 1 : 0);
    NEXT();
}

#ifdef OPTIMIZATION
op_gt_iconst:
{
    const int ci = READ_OPERAND();
    EXPECT_INT(_tos, "GT");
    _tos.data = (_tos.data > constants[ci]) ? 1 : 0;
    s[_sp] = _tos;
    ip += 4;
    NEXT();
}
#endif

op_jmp:
{
    const int offset = READ_OPERAND();
    ip += 4 + offset;
    NEXT();
}

op_jif:
{
    const int offset = READ_OPERAND();
    Value cond = pop();
    EXPECT_INT(cond, "JIF");
    ip += (cond.data == 0) ? (4 + offset) : 4;
    NEXT();
}

op_load:
{
    const int slot = READ_OPERAND();
    ip += 4;
    const int base = peekFrame();
    push(s[base + slot]);
    NEXT();
}

op_store:
{
    const int slot = READ_OPERAND();
    ip += 4;
    const int base = peekFrame();
    s[base + slot] = pop();
    NEXT();
}

op_call:
{
    const int funcIdx = READ_OPERAND();
    const FunctionInfo &func = functions[funcIdx];
    const int frameBase = _sp + 1 - func.paramCount;
    pushFrame(frameBase);

    for (int i = 0; i < func.localCount; i++)
        pushInt(0);

    pushCall(ip + 4);
    ip = func.codeOffset;
    NEXT();
}

op_ret:
{
    Value retVal = pop();
    _sp = popFrame() - 1;
    push(retVal);
    ip = popCall();
    NEXT();
}

op_print:
{
    Value v = pop();
    if (v.type == ValueType::INTEGER)
        std::cout << v.data;
    else
        std::cout << strPool[v.data];
    NEXT();
}

op_pop:
{
    pop();
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
