#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>
#include "Bytecode.h"

//
// JIT 编译器 — 将 Rua 字节码编译为 x86-64 机器码
//
// 仅在 OPTIMIZATION 宏下启用。
// 纯整数/算术/输出函数走 JIT，含字符串的函数退回到解释器。
//
// JIT 生成 x86-64 机器码，仅支持 x64 目标；32 位构建自动退回解释器。
//

#if defined(OPTIMIZATION) && (defined(_WIN64) || !defined(_WIN32))

// JIT 函数类型：最多 6 个 int64 参数，返回 int64
typedef int64_t (*JITFunc)(int64_t, int64_t, int64_t, int64_t, int64_t,
                           int64_t);

class JITCompiler {
  public:
    // x86-64 寄存器编号
    enum X86Reg : uint8_t {
        RAX = 0,
        RCX = 1,
        RDX = 2,
        RBX = 3,
        RSP = 4,
        RBP = 5,
        RSI = 6,
        RDI = 7,
        R8 = 8,
        R9 = 9,
        R10 = 10,
        R11 = 11,
        R12 = 12,
        R13 = 13,
        R14 = 14,
        R15 = 15,
    };

    // Rua 虚拟寄存器 → x86-64 物理寄存器固定映射
    // v0(r0) = RAX, v1(r1) = RDI, v2(r2) = RSI, …
    static int vregToPhys(int vreg)
    {
        // v0=RAX(返回值), v1=RBX(参数1,callee-saved)
        // v2=R12, v3=R13, v4=R14, v5=R15 (callee-saved)
        // v6=RSI, v7=RDI, v8=RDX, v9=RCX, v10=R8, v11=R9
        // v12=R10, v13=R11
        static const uint8_t map[] = {
            RAX, RBX, R12, R13, R14, R15, RSI, RDI, RDX, RCX, R8, R9, R10, R11,
        };
        if (vreg >= 0 && vreg < 14) return map[vreg];
        return -1;
    }

    static bool isCalleeSaved(int preg)
    {
        return preg == RBX || preg == RBP || preg == R12 || preg == R13
               || preg == R14 || preg == R15;
    }

  public:
    JITCompiler();
    ~JITCompiler();

    // 编译整个程序，填充 BytecodeProgram 中的 jitFunc
    void compile(BytecodeProgram& prog);

  private:
    // 判断函数是否适合 JIT（无 MOVS/无复杂操作）
    bool canJIT(const FunctionInfo& func, const BytecodeProgram& prog);

    // 编译单个函数，返回 JIT 函数指针
    JITFunc compileFunction(int funcIdx, const FunctionInfo& func,
                            const BytecodeProgram& prog);

    // ========== x86-64 编码器 ==========

    // 代码缓冲：mmap 分配的可执行内存
    uint8_t* codeBuf = nullptr;
    size_t codeCap = 0;
    size_t codePos = 0;

    void ensure(size_t n);
    void emit8(uint8_t v);
    void emit32(int32_t v);
    void emit64(int64_t v);
    void emitBytes(const void* src, size_t n);

    // REX 前缀 — W=1 表示 64 位操作数
    void emitREX(bool w, bool r, bool x, bool b);

    // ModRM: mod(2) | reg(3) | rm(3)
    //   mod=3 : 寄存器-寄存器
    //   oreg = 通过 reg 字段编码的操作码扩展或目标寄存器
    //   ereg = rm 字段编码的源/目标寄存器
    void emitModRM(uint8_t mod, uint8_t oreg, uint8_t ereg);

    // 带 SIB 的 ModRM (用于 [base + index*scale + disp])
    void emitModRMSIB(uint8_t mod, uint8_t oreg, uint8_t base, uint8_t index,
                      uint8_t scale);

    // ---------- 指令发射 ----------

    // mov preg(dst), preg(src) — 寄存器间复制
    void emitMOV(uint8_t dst, uint8_t src);

    // mov preg, imm32 — 加载 32 位立即数（零扩展到 64 位）
    void emitMOVi(uint8_t reg, int32_t imm);

    // mov preg, imm64
    void emitMOVi64(uint8_t reg, int64_t imm);

    // mov preg(dst), [rbp + offset] — 从栈加载
    void emitMOVfromStack(uint8_t dst, int32_t offset);

    // mov preg(dst), [rsp + disp8] — 从 rsp 相对栈加载（SIB 寻址）
    void emitMOVfromRSP(uint8_t dst, int8_t offset);

    // mov [rbp + offset], preg(src) — 存入栈
    void emitMOVtoStack(int32_t offset, uint8_t src);

    // add dst, src — dst += src
    void emitADD(uint8_t dst, uint8_t src);

    // sub dst, src — dst -= src
    void emitSUB(uint8_t dst, uint8_t src);

    // imul dst, src — dst *= src
    void emitIMUL(uint8_t dst, uint8_t src);

    // xor dst, src — dst ^= src
    void emitXOR(uint8_t dst, uint8_t src);

    // cmp a, b — 比较 a - b
    void emitCMP(uint8_t a, uint8_t b);

    // test a, a — 设置标志位
    void emitTEST(uint8_t a);

    // setcc r — 根据条件码设置字节
    void emitSETcc(uint8_t reg, const char* cc);

    // movzx dst32, src8 — 零扩展字节到 32 位
    void emitMOVZX(uint8_t dst32, uint8_t src8);

    // call rel32 — 直接调用（相对偏移）
    void emitCALLrel(int32_t offset);

    // call reg — 间接调用
    void emitCALLreg(uint8_t reg);

    // jmp rel32 — 无条件跳转
    void emitJMPrel(int32_t offset);

    // jcc rel32 — 条件跳转
    void emitJCCrel(const char* cc, int32_t offset);

    // push reg
    void emitPUSH(uint8_t reg);

    // pop reg
    void emitPOP(uint8_t reg);

    // ret
    void emitRET();

    // nop 对齐
    void emitNOP();

    // ---------- 标签与回填 ----------
    struct Label {
        size_t fixupPos; // 需要回填的位置
        int32_t target;  // 目标偏移（patch 后设置）
    };

    // 在当前位置创建一个标签（供前向跳转回填）
    size_t makeLabel();
    // 在当前位置回填 label，所有引用此 label 的位置跳转到当前位置
    void bindLabel(size_t label);
    // 在当前位置写一个跳转，跳转到 label（后向跳转直接写；前向跳转记入 fixup
    // 列表）
    void emitJumpToLabel(const char* cc, size_t label);
    void emitJumpToLabel(size_t label); // 无条件

  private:
    std::vector<Label> labels;
    std::vector<std::pair<size_t, size_t>> forwardJumps; // (需要回填的codePos,
                                                         // labelIndex)
};

#endif // OPTIMIZATION && (WIN64 || non-Windows)
