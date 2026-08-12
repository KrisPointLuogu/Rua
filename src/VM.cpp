/*
 * 寄存器式虚拟机 —— VM (优化版)
 *
 * 优化策略：
 *   1. 直接线程分发（computed goto）
 *   2. 帧基址局部变量 + base 指针寄存器访问
 *   3. int32 直接内存读取
 *   4. 循环用指针步进代替索引计算
 */

#include "VM.h"
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

// 运行时数组对象：各维长度 + 扁平数据（row-major）
struct 数组对象 {
    std::vector<int> dims;
    std::vector<Value> data;
};

void Value::print() const
{
    if (type == ValueType::INTEGER)
        std::cout << data;
    else if (type == ValueType::STRING)
        std::cout << "(string)";
    else
        std::cout << "(数组)";
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
    vector<数组对象> arrayPool;
    // ARRGETN/ARRSETN 的变长下标缓冲（须在分派标签前声明，避免跨标签构造）
    vector<int> 数组下标;

#if defined(__GNUC__) || defined(__clang__)
    // GCC/Clang: computed goto（最快）
    static void* dispatch[] = {
        &&op_halt,  &&op_movi, &&op_movs, &&op_mov,  &&op_add,  &&op_sub,
        &&op_mul,   &&op_div,  &&op_mod,  &&op_eq,   &&op_ne,   &&op_lt,
        &&op_gt,    &&op_jmp,  &&op_jif,  &&op_push, &&op_call, &&op_ret,
        &&op_print, &&op_le,   &&op_ge,   &&op_arrnew, &&op_arrget,
        &&op_arrset, &&op_arrdimset, &&op_arrgetn, &&op_arrsetn,
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

// MSVC 的 switch 分派需要 case 标签；GCC/Clang 走 computed goto，无需 case
// （switch 按 int 提升，故 case 需 static_cast<int>）
#if defined(__GNUC__) || defined(__clang__)
#define OP_CASE(x)
#else
#define OP_CASE(x) case static_cast<int>(Opcode::x):
#endif

OP_CASE(HALT)
op_halt:
    goto op_halt_end;
OP_CASE(MOVI)
op_movi: {
    int rd = code[ip + 1], ci = I32(ip + 4);
    base[rd] = Value(constants[ci]);
    ip += 8;
    NEXT();
}
OP_CASE(MOVS)
op_movs: {
    int rd = code[ip + 1], si = I32(ip + 4);
    strPool.push_back(strings[si]);
    base[rd] = Value(static_cast<int>(strPool.size()) - 1, ValueType::STRING);
    ip += 8;
    NEXT();
}
OP_CASE(MOV)
op_mov: {
    int rd = code[ip + 1], rs = code[ip + 2];
    base[rd] = base[rs];
    ip += 8;
    NEXT();
}
OP_CASE(ADD)
op_add: {
    int rd = code[ip + 1], rs1 = code[ip + 2], rs2 = code[ip + 3];
    Value &b = base[rs1], &c = base[rs2];
    if (b.type == ValueType::INTEGER && c.type == ValueType::INTEGER)
        base[rd] = Value(b.data + c.data);
    else if (b.type == ValueType::STRING && c.type == ValueType::STRING) {
        strPool.push_back(strPool[b.data] + strPool[c.data]);
        base[rd]
            = Value(static_cast<int>(strPool.size()) - 1, ValueType::STRING);
    } else {
        throw VMError("加法操作数类型不匹配");
    }
    ip += 8;
    NEXT();
}
OP_CASE(SUB)
op_sub: {
    int rd = code[ip + 1], rs1 = code[ip + 2], rs2 = code[ip + 3];
    base[rd] = Value(base[rs1].data - base[rs2].data);
    ip += 8;
    NEXT();
}
OP_CASE(MUL)
op_mul: {
    int rd = code[ip + 1], rs1 = code[ip + 2], rs2 = code[ip + 3];
    base[rd] = Value(base[rs1].data * base[rs2].data);
    ip += 8;
    NEXT();
}
OP_CASE(DIV)
op_div: {
    int rd = code[ip + 1], rs1 = code[ip + 2], rs2 = code[ip + 3];
    int c = base[rs2].data;
    if (c == 0) throw VMError("除数为零");
    base[rd] = Value(base[rs1].data / c);
    ip += 8;
    NEXT();
}
OP_CASE(MOD)
op_mod: {
    int rd = code[ip + 1], rs1 = code[ip + 2], rs2 = code[ip + 3];
    int c = base[rs2].data;
    if (c == 0) throw VMError("除数为零");
    base[rd] = Value(base[rs1].data % c);
    ip += 8;
    NEXT();
}
OP_CASE(EQ)
op_eq: {
    int rd = code[ip + 1], rs1 = code[ip + 2], rs2 = code[ip + 3];
    Value &b = base[rs1], &c = base[rs2];
    bool 相等;
    if (b.type != c.type)
        相等 = false;
    else if (b.type == ValueType::STRING)
        相等 = strPool[b.data] == strPool[c.data];
    else
        相等 = b.data == c.data;
    base[rd] = Value(相等 ? 1 : 0);
    ip += 8;
    NEXT();
}
OP_CASE(NE)
op_ne: {
    int rd = code[ip + 1], rs1 = code[ip + 2], rs2 = code[ip + 3];
    Value &b = base[rs1], &c = base[rs2];
    bool 相等;
    if (b.type != c.type)
        相等 = false;
    else if (b.type == ValueType::STRING)
        相等 = strPool[b.data] == strPool[c.data];
    else
        相等 = b.data == c.data;
    base[rd] = Value(相等 ? 0 : 1);
    ip += 8;
    NEXT();
}
OP_CASE(LT)
op_lt: {
    int rd = code[ip + 1], rs1 = code[ip + 2], rs2 = code[ip + 3];
    base[rd] = Value(base[rs1].data < base[rs2].data ? 1 : 0);
    ip += 8;
    NEXT();
}
OP_CASE(GT)
op_gt: {
    int rd = code[ip + 1], rs1 = code[ip + 2], rs2 = code[ip + 3];
    base[rd] = Value(base[rs1].data > base[rs2].data ? 1 : 0);
    ip += 8;
    NEXT();
}
OP_CASE(LE)
op_le: {
    int rd = code[ip + 1], rs1 = code[ip + 2], rs2 = code[ip + 3];
    base[rd] = Value(base[rs1].data <= base[rs2].data ? 1 : 0);
    ip += 8;
    NEXT();
}
OP_CASE(GE)
op_ge: {
    int rd = code[ip + 1], rs1 = code[ip + 2], rs2 = code[ip + 3];
    base[rd] = Value(base[rs1].data >= base[rs2].data ? 1 : 0);
    ip += 8;
    NEXT();
}
OP_CASE(ARRNEW)
op_arrnew: {
    int rd = code[ip + 1], rsSize = code[ip + 2], rsInit = code[ip + 3];
    int size = base[rsSize].data;
    if (size < 1) throw VMError("数组长度必须为正数");
    arrayPool.push_back(数组对象{ { size }, vector<Value>(size, base[rsInit]) });
    base[rd] = Value(static_cast<int>(arrayPool.size()) - 1, ValueType::ARRAY);
    ip += 8;
    NEXT();
}
OP_CASE(ARRDIMSET)
op_arrdimset: {
    // rd 槽位未使用，rs1=数组句柄，rs2=维度长度，extra=维度索引
    int rsArr = code[ip + 2], rsVal = code[ip + 3];
    int dimIdx = I32(ip + 4);
    Value& h = base[rsArr];
    if (h.type != ValueType::ARRAY) throw VMError("索引的目标不是数组");
    auto& arr = arrayPool[h.data];
    if (dimIdx < 0) throw VMError("数组维度索引非法");
    if (dimIdx >= static_cast<int>(arr.dims.size()))
        arr.dims.resize(dimIdx + 1, 1);
    arr.dims[dimIdx] = base[rsVal].data;
    ip += 8;
    NEXT();
}
OP_CASE(ARRGET)
op_arrget: {
    int rd = code[ip + 1], rsArr = code[ip + 2], rsIdx = code[ip + 3];
    Value& h = base[rsArr];
    if (h.type != ValueType::ARRAY) throw VMError("索引的目标不是数组");
    auto& arr = arrayPool[h.data];
    if (arr.dims.size() > 1) throw VMError("数组下标数量不足");
    int idx = base[rsIdx].data;
    if (idx < 0 || idx >= static_cast<int>(arr.data.size()))
        throw VMError("数组下标越界");
    base[rd] = arr.data[idx];
    ip += 8;
    NEXT();
}
OP_CASE(ARRSET)
op_arrset: {
    int rsVal = code[ip + 1], rsArr = code[ip + 2], rsIdx = code[ip + 3];
    Value& h = base[rsArr];
    if (h.type != ValueType::ARRAY) throw VMError("索引的目标不是数组");
    auto& arr = arrayPool[h.data];
    if (arr.dims.size() > 1) throw VMError("数组下标数量不足");
    int idx = base[rsIdx].data;
    if (idx < 0 || idx >= static_cast<int>(arr.data.size()))
        throw VMError("数组下标越界");
    arr.data[idx] = base[rsVal];
    ip += 8;
    NEXT();
}
OP_CASE(ARRGETN)
op_arrgetn: {
    int rd = code[ip + 1], rsArr = code[ip + 2];
    int count = I32(ip + 4);
    Value& h = base[rsArr];
    if (h.type != ValueType::ARRAY) throw VMError("索引的目标不是数组");
    数组下标.resize(count);
    for (int k = count - 1; k >= 0; k--) { 数组下标[k] = s[_sp].data; _sp--; }
    int arrIdx = h.data;
    int consumed = 0;
    while (consumed < count) {
        auto& arr = arrayPool[arrIdx];
        int n = static_cast<int>(arr.dims.size());
        if (n <= 0 || consumed + n > count) throw VMError("数组下标数量不足");
        int local = 0;
        for (int k = 0; k < n; k++) {
            int idx = 数组下标[consumed + k];
            int dim = arr.dims[k];
            if (idx < 0 || idx >= dim) throw VMError("数组下标越界");
            local = (k == 0) ? idx : local * dim + idx;
        }
        consumed += n;
        if (consumed == count) {
            base[rd] = arr.data[local];
            break;
        }
        Value elem = arr.data[local];
        if (elem.type != ValueType::ARRAY) throw VMError("索引的目标不是数组");
        arrIdx = elem.data;
    }
    ip += 8;
    NEXT();
}
OP_CASE(ARRSETN)
op_arrsetn: {
    int rsVal = code[ip + 1], rsArr = code[ip + 2];
    int count = I32(ip + 4);
    Value& h = base[rsArr];
    if (h.type != ValueType::ARRAY) throw VMError("索引的目标不是数组");
    数组下标.resize(count);
    for (int k = count - 1; k >= 0; k--) { 数组下标[k] = s[_sp].data; _sp--; }
    int arrIdx = h.data;
    int consumed = 0;
    int finalArrIdx = -1;
    int finalLocal = -1;
    while (consumed < count) {
        auto& arr = arrayPool[arrIdx];
        int n = static_cast<int>(arr.dims.size());
        if (n <= 0 || consumed + n > count) throw VMError("数组下标数量不足");
        int local = 0;
        for (int k = 0; k < n; k++) {
            int idx = 数组下标[consumed + k];
            int dim = arr.dims[k];
            if (idx < 0 || idx >= dim) throw VMError("数组下标越界");
            local = (k == 0) ? idx : local * dim + idx;
        }
        consumed += n;
        if (consumed == count) {
            finalArrIdx = arrIdx;
            finalLocal = local;
            break;
        }
        Value elem = arr.data[local];
        if (elem.type != ValueType::ARRAY) throw VMError("索引的目标不是数组");
        arrIdx = elem.data;
    }
    arrayPool[finalArrIdx].data[finalLocal] = base[rsVal];
    ip += 8;
    NEXT();
}
OP_CASE(JMP)
op_jmp: {
    int offset = I32(ip + 4);
    ip += 8 + offset;
    NEXT();
}
OP_CASE(JIF)
op_jif: {
    int rs1 = code[ip + 2], offset = I32(ip + 4);
    ip += (base[rs1].data == 0) ? (8 + offset) : 8;
    NEXT();
}
OP_CASE(PUSH)
op_push: {
    int rs1 = code[ip + 2];
    s[++_sp] = base[rs1];
    ip += 8;
    NEXT();
}
OP_CASE(CALL)
op_call: {
    int funcIdx = I32(ip + 4);

#ifdef OPTIMIZATION
    // JIT 快速路径：仅当所有参数均为整数时启用
    // 数组/字符串句柄经 JIT 函数往返会丢失 Value 类型（句柄被当作整数），
    // 故遇非整数参数退回解释器 CALL，以完整保留 Value 类型
    if (fnJIT[funcIdx]) {
        int pCnt = fnPC[funcIdx];
        bool 参数全整数 = true;
        for (int i = 0; i < pCnt && i < 6; i++) {
            if (s[_sp - pCnt + 1 + i].type != ValueType::INTEGER) {
                参数全整数 = false;
                break;
            }
        }
        if (参数全整数) {
            JITFunc jitFn = (JITFunc)fnJIT[funcIdx];
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
OP_CASE(RET)
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
OP_CASE(PRINT)
op_print: {
    int rs1 = code[ip + 2];
    Value& v = base[rs1];
    if (v.type == ValueType::INTEGER)
        std::cout << v.data;
    else if (v.type == ValueType::STRING)
        std::cout << strPool[v.data];
    else
        std::cout << "(数组)";
    std::cout << '\n';
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
#undef OP_CASE
