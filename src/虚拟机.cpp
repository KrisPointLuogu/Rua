/*
 * 寄存器式虚拟机 —— VM (优化版)
 *
 * 优化策略：
 *   1. 直接线程分发（computed goto）
 *   2. 帧基址局部变量 + base 指针寄存器访问
 *   3. int32 直接内存读取
 *   4. 循环用指针步进代替索引计算
 */

#include "虚拟机.h"
#include <cstring>
#include <iostream>
#include <sstream>
#ifdef _DEBUG
#include <chrono>
#endif
#ifdef OPTIMIZATION
typedef int64_t (*JITFunc)(int64_t, int64_t, int64_t, int64_t, int64_t,
                           int64_t);
#endif

using std::string;
using std::vector;

void Value::print() const
{
    if (type == ValueType::INTEGER)
        std::cout << data;
    else
        std::cout << "(string)";
}

VMError::VMError(const string& message) : std::runtime_error(message) {}

VM::VM()
  : stackData(std::make_unique<Value[]>(STACK_CAP)),
    callStackData(std::make_unique<int[]>(CALL_CAP)),
    frameStackData(std::make_unique<int[]>(FRAME_CAP)),
    sp(-1),
    cp(-1),
    fpStack(-1),
    program(nullptr),
    ip(0)
{
}

void VM::run(const BytecodeProgram& prog)
{
    program = &prog;

    if (prog.code.empty()) throw VMError("字节码为空");

    const auto& code = prog.code;
    const auto& constants = prog.constants;
    const auto& strings = prog.strings;
    const auto& functions = prog.functions;

    // 函数信息 SoA 缓存
    int fnCount = (int)functions.size();
    int fnPC[256], fnRC[256], fnCO[256];
#ifdef OPTIMIZATION
    void* fnJIT[256];
#endif
    for (int i = 0; i < fnCount; i++) {
        fnPC[i] = functions[i].paramCount;
        fnRC[i] = functions[i].regCount;
        fnCO[i] = functions[i].codeOffset;
#ifdef OPTIMIZATION
        fnJIT[i] = functions[i].jitFunc;
#endif
    }

#ifdef _DEBUG
    auto 开始 = std::chrono::high_resolution_clock::now();
#endif

    Value* __restrict s = stackData.get();
    int* __restrict cs = callStackData.get();
    int* __restrict fs = frameStackData.get();

    int _sp = -1, _cp = -1, _fs = -1, _fp = 0;
    Value* base = s;
    vector<string> strPool;

#if defined(__GNUC__) || defined(__clang__)
    // GCC/Clang: computed goto（最快）
    static void* dispatch[] = {
        &&op_halt,  &&op_movi, &&op_movs, &&op_mov,  &&op_add,  &&op_sub,
        &&op_mul,   &&op_div,  &&op_mod,  &&op_eq,   &&op_ne,   &&op_lt,
        &&op_gt,    &&op_jmp,  &&op_jif,  &&op_push, &&op_call, &&op_ret,
        &&op_print, &&op_le,   &&op_ge,
    };

#define I32(a)                                                                 \
    ({                                                                         \
        int _v;                                                                \
        __builtin_memcpy(&_v, code.data() + (a), 4);                           \
        _v;                                                                    \
    })
#define NEXT() goto* dispatch[code[ip]]

    ip = 0;
    NEXT();
#else
    // MSVC: switch-based dispatch
#define I32(a) (*(int*)(code.data() + (a)))
#define NEXT() goto dispatch_switch
dispatch_switch:
    switch (code[ip]) {
#endif

op_halt:
    goto op_halt_end;
op_movi: {
    int rd = code[ip + 1], ci = I32(ip + 4);
    base[rd] = Value(constants[ci]);
    ip += 8;
    NEXT();
}
op_movs: {
    int rd = code[ip + 1], si = I32(ip + 4);
    strPool.push_back(strings[si]);
    base[rd] = Value(static_cast<int>(strPool.size()) - 1, ValueType::STRING);
    ip += 8;
    NEXT();
}
op_mov: {
    int rd = code[ip + 1], rs = code[ip + 2];
    base[rd] = base[rs];
    ip += 8;
    NEXT();
}
op_add: {
    int rd = code[ip + 1], rs1 = code[ip + 2], rs2 = code[ip + 3];
    Value &b = base[rs1], &c = base[rs2];
    if (b.type == ValueType::INTEGER)
        base[rd] = Value(b.data + c.data);
    else {
        strPool.push_back(strPool[b.data] + strPool[c.data]);
        base[rd]
            = Value(static_cast<int>(strPool.size()) - 1, ValueType::STRING);
    }
    ip += 8;
    NEXT();
}
op_sub: {
    int rd = code[ip + 1], rs1 = code[ip + 2], rs2 = code[ip + 3];
    base[rd] = Value(base[rs1].data - base[rs2].data);
    ip += 8;
    NEXT();
}
op_mul: {
    int rd = code[ip + 1], rs1 = code[ip + 2], rs2 = code[ip + 3];
    base[rd] = Value(base[rs1].data * base[rs2].data);
    ip += 8;
    NEXT();
}
op_div: {
    int rd = code[ip + 1], rs1 = code[ip + 2], rs2 = code[ip + 3];
    int c = base[rs2].data;
    if (c == 0) throw VMError("除数为零");
    base[rd] = Value(base[rs1].data / c);
    ip += 8;
    NEXT();
}
op_mod: {
    int rd = code[ip + 1], rs1 = code[ip + 2], rs2 = code[ip + 3];
    int c = base[rs2].data;
    if (c == 0) throw VMError("除数为零");
    base[rd] = Value(base[rs1].data % c);
    ip += 8;
    NEXT();
}
op_eq: {
    int rd = code[ip + 1], rs1 = code[ip + 2], rs2 = code[ip + 3];
    Value &b = base[rs1], &c = base[rs2];
    base[rd] = Value((b.type == c.type) & (b.data == c.data) ? 1 : 0);
    ip += 8;
    NEXT();
}
op_ne: {
    int rd = code[ip + 1], rs1 = code[ip + 2], rs2 = code[ip + 3];
    Value &b = base[rs1], &c = base[rs2];
    base[rd] = Value((b.type != c.type) | (b.data != c.data) ? 1 : 0);
    ip += 8;
    NEXT();
}
op_lt: {
    int rd = code[ip + 1], rs1 = code[ip + 2], rs2 = code[ip + 3];
    base[rd] = Value(base[rs1].data < base[rs2].data ? 1 : 0);
    ip += 8;
    NEXT();
}
op_gt: {
    int rd = code[ip + 1], rs1 = code[ip + 2], rs2 = code[ip + 3];
    base[rd] = Value(base[rs1].data > base[rs2].data ? 1 : 0);
    ip += 8;
    NEXT();
}
op_le: {
    int rd = code[ip + 1], rs1 = code[ip + 2], rs2 = code[ip + 3];
    base[rd] = Value(base[rs1].data <= base[rs2].data ? 1 : 0);
    ip += 8;
    NEXT();
}
op_ge: {
    int rd = code[ip + 1], rs1 = code[ip + 2], rs2 = code[ip + 3];
    base[rd] = Value(base[rs1].data >= base[rs2].data ? 1 : 0);
    ip += 8;
    NEXT();
}
op_jmp: {
    int offset = I32(ip + 4);
    ip += 8 + offset;
    NEXT();
}
op_jif: {
    int rs1 = code[ip + 2], offset = I32(ip + 4);
    ip += (base[rs1].data == 0) ? (8 + offset) : 8;
    NEXT();
}
op_push: {
    int rs1 = code[ip + 2];
    s[++_sp] = base[rs1];
    ip += 8;
    NEXT();
}
op_call: {
    int funcIdx = I32(ip + 4);

#ifdef OPTIMIZATION
    // JIT 快速路径
    if (fnJIT[funcIdx]) {
        JITFunc jitFn = (JITFunc)fnJIT[funcIdx];
        int pCnt = fnPC[funcIdx];
        int64_t args[6] = { 0, 0, 0, 0, 0, 0 };
        for (int i = 0; i < pCnt && i < 6; i++)
            args[i] = s[_sp - pCnt + 1 + i].data;
        _sp -= pCnt + 1; // 弹出参数 + r0 占位
        int64_t result
            = jitFn(args[0], args[1], args[2], args[3], args[4], args[5]);
        s[_fp] = Value((int)result);
        ip += 8;
        NEXT();
    }
#endif

    int pCnt = fnPC[funcIdx], rCnt = fnRC[funcIdx];
    int newFP = _sp - pCnt;
    fs[++_fs] = newFP;

    Value* dst = s + newFP + pCnt + 1;
    for (int i = rCnt - pCnt - 1; i > 0; i--) *dst++ = Value(0);

    _sp = newFP + rCnt - 1;
    _fp = newFP;
    base = s + newFP;
    cs[++_cp] = ip + 8;
    ip = fnCO[funcIdx];
    NEXT();
}
op_ret: {
    Value retVal = base[0];
    _sp = _fp - 1;
    --_fs;
    _fp = (_fs >= 0) ? fs[_fs] : 0;
    base = s + _fp;
    base[0] = retVal;
    ip = cs[_cp--];
    NEXT();
}
op_print: {
    int rs1 = code[ip + 2];
    Value& v = base[rs1];
    if (v.type == ValueType::INTEGER)
        std::cout << v.data;
    else
        std::cout << strPool[v.data];
    ip += 8;
    NEXT();
}
#if !(defined(__GNUC__) || defined(__clang__))
} // switch
#endif
op_halt_end: sp = _sp;
cp = _cp;
fpStack = _fs;

#ifdef _DEBUG
{
    auto 结束 = std::chrono::high_resolution_clock::now();
    auto 耗时
        = std::chrono::duration_cast<std::chrono::microseconds>(结束 - 开始)
              .count();
    std::cout << "\n[DEBUG] 字节码执行耗时: " << 耗时 << " 微秒 ("
              << (耗时 / 1000.0) << " 毫秒)\n";
}
#endif
}

#undef I32
#undef NEXT
