/*
 * JIT 编译器 — 将 Rua 字节码编译为 x86-64 机器码
 *
 * OPTIMIZATION 宏启用时才编译此文件。
 * 使用 arch:: 分配可执行内存，手写 x86-64 字节码。
 *
 * 寄存器映射 (固定):
 *   v0(RAX) v1(RBX) v2(R12) v3(R13) v4(R14) v5(R15)  (callee-saved)
 *   v6(RSI) v7(RDI) v8(RDX) v9(RCX) v10(R8) v11(R9)  (caller-saved)
 *   v12(R10) v13(R11)
 *   v14+ → 栈溢出
 */

#include "JIT.h"
#include <iostream>
#include <vector>
#include "JITPlatform.h"

#ifdef OPTIMIZATION

#include <iostream>

// JIT 辅助函数：PRINT 调用此函数输出整数
extern "C" void jit_print_int(int64_t val)
{
    std::cout << val << '\n';
}

// JIT 函数指针全局表（供 JIT 编译的代码调用其他 JIT 函数时使用）
static int64_t g_jitTable[256];
static int g_jitTableSize = 0;

using std::string;
using std::vector;

// ==================== JITCompiler ====================

JITCompiler::JITCompiler()
{
    size_t pageSize = arch::getPageSize();
    codeCap = pageSize * 64; // 256 KB 初始
    codeBuf = arch::allocateExecutableMemory(codeCap);
    if (!codeBuf) { codeCap = 0; }
    codePos = 0;
}

JITCompiler::~JITCompiler()
{
    // 不释放 codeBuf，JIT 编译的代码需要在 VM 执行期间存活
    // 进程退出时 OS 会自动回收
}

void JITCompiler::ensure(size_t n)
{
    if (!codeBuf || codePos + n > codeCap) {
        // 扩大容量
        size_t newCap = codeCap * 2 + n;
        uint8_t* newBuf = arch::allocateExecutableMemory(newCap);
        if (!newBuf) {
            std::cerr << "JIT: 内存分配失败\n";
            return;
        }
        if (codeBuf) memcpy(newBuf, codeBuf, codePos);
        codeBuf = newBuf;
        codeCap = newCap;
    }
}

void JITCompiler::emit8(uint8_t v)
{
    ensure(1);
    codeBuf[codePos++] = v;
}
void JITCompiler::emit32(int32_t v)
{
    ensure(4);
    memcpy(codeBuf + codePos, &v, 4);
    codePos += 4;
}
void JITCompiler::emit64(int64_t v)
{
    ensure(8);
    memcpy(codeBuf + codePos, &v, 8);
    codePos += 8;
}
void JITCompiler::emitBytes(const void* src, size_t n)
{
    ensure(n);
    memcpy(codeBuf + codePos, src, n);
    codePos += n;
}

void JITCompiler::emitREX(bool w, bool r, bool x, bool b)
{
    uint8_t rex = 0x40 | (w << 3) | (r << 2) | (x << 1) | b;
    if (rex != 0x40) emit8(rex);
}

void JITCompiler::emitModRM(uint8_t mod, uint8_t oreg, uint8_t ereg)
{ emit8((mod << 6) | ((oreg & 7) << 3) | (ereg & 7)); }

void JITCompiler::emitModRMSIB(uint8_t mod, uint8_t oreg, uint8_t base,
                               uint8_t index, uint8_t scale)
{
    emit8((mod << 6) | ((oreg & 7) << 3) | 4); // SIB 标志
    emit8((scale << 6) | ((index & 7) << 3) | (base & 7));
}

// ==================== 指令发射 ====================

void JITCompiler::emitMOV(uint8_t dst, uint8_t src)
{
    // mov dst, src : 48 8B /r  (dst = src, r64 ← r/m64)
    if (dst == src) return;
    emitREX(1, (dst & 8), 0, (src & 8));
    emit8(0x8B);
    emitModRM(3, dst & 7, src & 7);
}

void JITCompiler::emitMOVi(uint8_t reg, int32_t imm)
{
    // mov eax, imm32 (零扩展到 64 位): B8+r
    emitREX(0, 0, 0, (reg & 8));
    emit8(0xB8 | (reg & 7));
    emit32(imm);
}

void JITCompiler::emitMOVi64(uint8_t reg, int64_t imm)
{
    // mov rax, imm64: REX.W B8+r
    emitREX(1, 0, 0, (reg & 8));
    emit8(0xB8 | (reg & 7));
    emit64(imm);
}

void JITCompiler::emitMOVfromStack(uint8_t dst, int32_t offset)
{
    // mov dst, [rbp + offset]: 48 8B 45/85 disp8/disp32
    uint8_t mod = (offset >= -128 && offset <= 127) ? 1 : 2;
    emitREX(1, (dst & 8), 0, 0);
    emit8(0x8B);
    emitModRM(mod, dst & 7, RBP);
    if (mod == 1)
        emit8((int8_t)offset);
    else
        emit32(offset);
}

void JITCompiler::emitMOVtoStack(int32_t offset, uint8_t src)
{
    // mov [rbp + offset], src: 48 89 45/85 disp8/disp32
    uint8_t mod = (offset >= -128 && offset <= 127) ? 1 : 2;
    emitREX(1, (src & 8), 0, 0);
    emit8(0x89);
    emitModRM(mod, src & 7, RBP);
    if (mod == 1)
        emit8((int8_t)offset);
    else
        emit32(offset);
}

void JITCompiler::emitMOVfromRSP(uint8_t dst, int8_t offset)
{
    // mov dst, [rsp + offset]: 48 8B 44 24 disp8（RSP 为基址需 SIB）
    emitREX(1, (dst & 8), 0, 0);
    emit8(0x8B);
    emitModRMSIB(1, dst & 7, RSP, 4, 0); // index=4 表示无索引
    emit8(offset);
}

void JITCompiler::emitADD(uint8_t dst, uint8_t src)
{
    // add dst, src: 48 03 /r  (dst ← dst + src, r64 ← r/m64)
    emitREX(1, (dst & 8), 0, (src & 8));
    emit8(0x03);
    emitModRM(3, dst & 7, src & 7);
}

void JITCompiler::emitSUB(uint8_t dst, uint8_t src)
{
    // sub dst, src: 48 2B /r  (dst ← dst - src)
    emitREX(1, (dst & 8), 0, (src & 8));
    emit8(0x2B);
    emitModRM(3, dst & 7, src & 7);
}

void JITCompiler::emitIMUL(uint8_t dst, uint8_t src)
{
    // imul dst, src: 48 0F AF /r  (dst ← dst * src)
    emitREX(1, (dst & 8), 0, (src & 8));
    emit8(0x0F);
    emit8(0xAF);
    emitModRM(3, dst & 7, src & 7);
}

void JITCompiler::emitXOR(uint8_t dst, uint8_t src)
{
    // xor dst, src: 48 33 /r  (dst ← dst ^ src, r64 ← r/m64)
    emitREX(1, (dst & 8), 0, (src & 8));
    emit8(0x33);
    emitModRM(3, dst & 7, src & 7);
}

void JITCompiler::emitCMP(uint8_t a, uint8_t b)
{
    // cmp a, b: 48 3B /r  (compare a - b)
    emitREX(1, (a & 8), 0, (b & 8));
    emit8(0x3B);
    emitModRM(3, a & 7, b & 7);
}

void JITCompiler::emitTEST(uint8_t a)
{
    // test a, a: 48 85 /r
    emitREX(1, (a & 8), 0, (a & 8));
    emit8(0x85);
    emitModRM(3, a & 7, a & 7);
}

void JITCompiler::emitSETcc(uint8_t reg, const char* cc)
{
    // setcc byte_reg: 0F 9x (条件码在低 4 位)
    static const char* cclist[] = {
        "seto", "setno", "setb", "setae", "sete", "setne", "setbe", "seta",
        "sets", "setns", "setp", "setnp", "setl", "setge", "setle", "setg",
    };
    int op = -1;
    for (int i = 0; i < 16; i++)
        if (strcmp(cc, cclist[i]) == 0) {
            op = i;
            break;
        }
    if (op < 0) return;

    emitREX(0, 0, 0, (reg & 8));
    emit8(0x0F);
    emit8(0x90 | op);
    emitModRM(3, 0, reg & 7);
}

void JITCompiler::emitMOVZX(uint8_t dst32, uint8_t src8)
{
    // movzx dst32, src8: 0F B6 /r
    emitREX(0, (dst32 & 8), 0, (src8 & 8));
    emit8(0x0F);
    emit8(0xB6);
    emitModRM(3, dst32 & 7, src8 & 7);
}

void JITCompiler::emitCALLrel(int32_t offset)
{
    emit8(0xE8);
    emit32(offset);
}

void JITCompiler::emitCALLreg(uint8_t reg)
{
    // call reg: FF /2
    emitREX(0, 0, 0, (reg & 8));
    emit8(0xFF);
    emitModRM(3, 2, reg & 7);
}

void JITCompiler::emitJMPrel(int32_t offset)
{
    emit8(0xE9);
    emit32(offset);
}

void JITCompiler::emitJCCrel(const char* cc, int32_t offset)
{
    static const char* cclist[] = {
        "jo", "jno", "jb", "jae", "je", "jne", "jbe", "ja",
        "js", "jns", "jp", "jnp", "jl", "jge", "jle", "jg",
    };
    int op = -1;
    for (int i = 0; i < 16; i++)
        if (strcmp(cc, cclist[i]) == 0) {
            op = i;
            break;
        }
    if (op < 0) return;

    if (offset >= -128 && offset <= 127) {
        // 短跳转 (2 bytes)
        emit8(0x70 | op);
        emit8((int8_t)offset);
    } else {
        // 长跳转：0F 8x rel32 (6 bytes)
        emit8(0x0F);
        emit8(0x80 | op);
        emit32(offset);
    }
}

void JITCompiler::emitPUSH(uint8_t reg)
{
    // push reg: 50+r (64 位默认)
    emitREX(0, 0, 0, (reg & 8));
    emit8(0x50 | (reg & 7));
}

void JITCompiler::emitPOP(uint8_t reg)
{
    // pop reg: 58+r
    emitREX(0, 0, 0, (reg & 8));
    emit8(0x58 | (reg & 7));
}

void JITCompiler::emitRET() { emit8(0xC3); }

void JITCompiler::emitNOP() { emit8(0x90); }

// ==================== 标签与回填 ====================

size_t JITCompiler::makeLabel()
{
    labels.push_back({ (size_t)-1, 0 });
    return labels.size() - 1;
}

void JITCompiler::bindLabel(size_t label)
{
    labels[label].target = (int32_t)codePos;

    // 回填所有前向跳转
    for (auto it = forwardJumps.begin(); it != forwardJumps.end();) {
        if (it->second == label) {
            int32_t offset = (int32_t)codePos - (int32_t)(it->first + 4);
            memcpy(codeBuf + it->first, &offset, 4);
            it = forwardJumps.erase(it);
        } else {
            ++it;
        }
    }
}

void JITCompiler::emitJumpToLabel(const char* cc, size_t label)
{
    if (labels[label].target == 0 && labels[label].fixupPos == (size_t)-1) {
        // 前向跳转：留占位，记入 fixup
        labels[label].fixupPos = codePos;

        if (cc) {
            emit8(0x0F);
            static const char* cclist[] = {
                "jo", "jno", "jb", "jae", "je", "jne", "jbe", "ja",
                "js", "jns", "jp", "jnp", "jl", "jge", "jle", "jg",
            };
            int op = -1;
            for (int i = 0; i < 16; i++)
                if (strcmp(cc, cclist[i]) == 0) {
                    op = i;
                    break;
                }
            emit8(0x80 | op);
            emit32(0); // 占位
        } else {
            emit8(0xE9);
            emit32(0); // 占位
        }
        forwardJumps.push_back({ codePos - 4, label });
    } else {
        // 后向跳转：直接写偏移
        if (cc) {
            int32_t offShort = labels[label].target - (int32_t)(codePos + 2);
            int32_t offLong = labels[label].target - (int32_t)(codePos + 6);
            if (offShort >= -128 && offShort <= 127) {
                static const char* cclist[] = {
                    "jo", "jno", "jb", "jae", "je", "jne", "jbe", "ja",
                    "js", "jns", "jp", "jnp", "jl", "jge", "jle", "jg",
                };
                int op = -1;
                for (int i = 0; i < 16; i++)
                    if (strcmp(cc, cclist[i]) == 0) { op = i; break; }
                if (op >= 0) {
                    emit8(0x70 | op);
                    emit8((int8_t)offShort);
                }
            } else {
                emitJCCrel(cc, offLong);
            }
        } else {
            int32_t offset = labels[label].target - (int32_t)(codePos + 5);
            emitJMPrel(offset);
        }
    }
}

void JITCompiler::emitJumpToLabel(size_t label)
{ emitJumpToLabel(nullptr, label); }

// ==================== 函数编译 ====================

bool JITCompiler::canJIT(const FunctionInfo& func, const BytecodeProgram& prog)
{
    const auto& funcs = prog.functions;
    int myIndex = -1;
    for (size_t i = 0; i < funcs.size(); i++)
        if (funcs[i].codeOffset == func.codeOffset) { myIndex = (int)i; break; }

    // 基础判定：无字符串/数组/除模指令，参数不超 6 个
    auto 基础可JIT = [&](int idx) {
        const auto& f = funcs[idx];
        if (f.paramCount > 6) return false;
        int end = prog.getCodeSize();
        for (size_t k = idx + 1; k < funcs.size(); k++) {
            if (funcs[k].codeOffset > f.codeOffset) {
                end = funcs[k].codeOffset;
                break;
            }
        }
        for (int pc = f.codeOffset; pc < end; pc += 8) {
            Opcode op = (Opcode)prog.code[pc];
            if (op == Opcode::MOVS) return false;
            if (op == Opcode::DIV || op == Opcode::MOD) return false;
            if (op == Opcode::ARRNEW || op == Opcode::ARRGET
                || op == Opcode::ARRSET)
                return false;
        }
        return true;
    };

    // 提取每个函数的被调用者列表
    vector<vector<int>> callees(funcs.size());
    for (size_t i = 0; i < funcs.size(); i++) {
        int end = prog.getCodeSize();
        for (size_t k = i + 1; k < funcs.size(); k++) {
            if (funcs[k].codeOffset > funcs[i].codeOffset) {
                end = funcs[k].codeOffset;
                break;
            }
        }
        for (int pc = funcs[i].codeOffset; pc < end; pc += 8) {
            if ((Opcode)prog.code[pc] == Opcode::CALL) {
                int idx;
                memcpy(&idx, &prog.code[pc + 4], 4);
                callees[i].push_back(idx);
            }
        }
    }

    // 自底向上闭包：某函数不可 JIT 时，所有（间接）调用它的函数也不可 JIT
    vector<bool> ok(funcs.size(), false);
    for (size_t i = 0; i < funcs.size(); i++) ok[i] = 基础可JIT((int)i);
    bool changed = true;
    while (changed) {
        changed = false;
        for (size_t i = 0; i < funcs.size(); i++) {
            if (!ok[i]) continue;
            for (int c : callees[i]) {
                if (c >= 0 && c < (int)funcs.size() && !ok[c]) {
                    ok[i] = false;
                    changed = true;
                    break;
                }
            }
        }
    }
    return myIndex >= 0 && ok[myIndex];
}

JITFunc JITCompiler::compileFunction(int funcIdx, const FunctionInfo& func,
                                     const BytecodeProgram& prog)
{
    const auto& code = prog.code;

    // 跳转回填表（按字节码 PC 索引）
    struct PatchEntry {
        size_t codePos;
        int targetPc;
    };
    std::unordered_map<int, int32_t> pcLabels;
    std::vector<PatchEntry> forwardPatches;
    std::vector<size_t> skipEpilogue; // JMP 占位位置，需回填到 func_done
    const auto& constants = prog.constants;

    // 查找函数结束位置
    int endOff = prog.getCodeSize();
    for (size_t i = 0; i < prog.functions.size(); i++) {
        if (i == (size_t)funcIdx) continue;
        if (prog.functions[i].codeOffset > func.codeOffset
            && prog.functions[i].codeOffset < endOff)
            endOff = prog.functions[i].codeOffset;
    }
    // 对于最后一个函数，排除入口点代码（CALL + HALT）
    // 入口点在所有函数之后，以 HALT 结尾
    if (funcIdx == static_cast<int>(prog.functions.size()) - 1) {
        for (int pc = prog.getCodeSize() - 8; pc >= func.codeOffset; pc -= 8) {
            if ((Opcode)prog.code[pc] == Opcode::HALT) {
                endOff = pc;
                break;
            }
        }
    }

    // ----- 寄存器着色 -----
    // 扫描所有指令，找出实际用到的虚拟寄存器范围
    int maxVReg = 0;
    for (int pc = func.codeOffset; pc < endOff; pc += 8) {
        uint8_t rd = code[pc + 1];
        uint8_t rs1 = code[pc + 2];
        uint8_t rs2 = code[pc + 3];
        if (rd > maxVReg) maxVReg = rd;
        if (rs1 > maxVReg) maxVReg = rs1;
        if (rs2 > maxVReg) maxVReg = rs2;
    }
    // 确保参数寄存器也被计入
    if (func.paramCount > maxVReg) maxVReg = func.paramCount;

#ifdef _DEBUG
    std::cerr << "\n[JIT] ========== 编译函数 [" << funcIdx << "] " << func.name
              << " ==========\n";
    std::cerr << "[JIT]   参数数=" << func.paramCount << " 最大vreg=" << maxVReg
              << " 代码偏移=" << func.codeOffset << "\n";
    std::cerr << "[JIT]   寄存器分配:\n";
    for (int v = 0; v <= maxVReg && v < 14; v++) {
        int p = vregToPhys(v);
        const char* names[]
            = { "RAX", "RCX", "RDX", "RBX", "RSP", "RBP", "RSI", "RDI",
                "R8",  "R9",  "R10", "R11", "R12", "R13", "R14", "R15" };
        std::cerr << "     v" << v << " -> " << names[p]
                  << (isCalleeSaved(p) ? " (callee-saved)" : " (caller-saved)")
                  << "\n";
    }
    if (maxVReg >= 14)
        std::cerr << "     v14+ -> 栈溢出 (spillCount=" << (maxVReg - 13)
                  << ")\n";
#endif

    // 计算需要保存的 callee-saved 寄存器
    bool needRBX = (func.paramCount >= 1), needR12 = (func.paramCount >= 2),
         needR13 = (func.paramCount >= 3),
         needR14 = (func.paramCount >= 4), needR15 = (func.paramCount >= 5);
    for (int pc = func.codeOffset; pc < endOff; pc += 8) {
        uint8_t rd = code[pc + 1], rs1 = code[pc + 2], rs2 = code[pc + 3];
        int p = vregToPhys(rd);
        if (p == RBX) needRBX = true;
        if (p == R12) needR12 = true;
        if (p == R13) needR13 = true;
        if (p == R14) needR14 = true;
        if (p == R15) needR15 = true;
        if (vregToPhys(rs1) == RBX) needRBX = true;
        if (vregToPhys(rs1) == R12) needR12 = true;
        if (vregToPhys(rs1) == R13) needR13 = true;
        if (vregToPhys(rs1) == R14) needR14 = true;
        if (vregToPhys(rs1) == R15) needR15 = true;
        if (vregToPhys(rs2) == RBX) needRBX = true;
        if (vregToPhys(rs2) == R12) needR12 = true;
        if (vregToPhys(rs2) == R13) needR13 = true;
        if (vregToPhys(rs2) == R14) needR14 = true;
        if (vregToPhys(rs2) == R15) needR15 = true;
    }

    // 栈溢出槽位（v14+ 的寄存器需要 spill 到栈）
    // 每个溢出槽 8 字节
    int spillCount = maxVReg >= 14 ? (maxVReg - 13) : 0;
    int spillBase = 0; // 在 pushedBytes 计算后设置

#ifdef _DEBUG
    std::cerr << "[JIT]   需保存callee-saved: RBX=" << needRBX
              << " R12=" << needR12 << " R13=" << needR13 << " R14=" << needR14
              << " R15=" << needR15 << "\n";
    std::cerr << "[JIT]   溢出槽位=" << spillCount << "\n";
#endif

    // ===== 函数序言 =====
    size_t funcStart = codePos;

#ifdef _DEBUG
    std::cerr << "[JIT]   prologue @ offset " << codePos << ":\n";
#endif

    // push rbp
    emitPUSH(RBP);
    // mov rbp, rsp
    emitMOV(RBP, RSP);

    // 保存 callee-saved 寄存器（必须在参数复制之前）
    if (needRBX) emitPUSH(RBX);
    if (needR12) emitPUSH(R12);
    if (needR13) emitPUSH(R13);
    if (needR14) emitPUSH(R14);
    if (needR15) emitPUSH(R15);

    // 分配栈空间（溢出 + 16 字节对齐）
    int stackFrame = spillCount * 8;
    // 对齐：push rbp = 8, 每个 callee-saved push = 8
    int pushedBytes = 8; // rbp
    if (needRBX) pushedBytes += 8;
    if (needR12) pushedBytes += 8;
    if (needR13) pushedBytes += 8;
    if (needR14) pushedBytes += 8;
    if (needR15) pushedBytes += 8;
    int alignedFrame = (stackFrame + 15) & ~15;
    // SysV ABI: 入口 RSP ≡ 8(mod 16)（CALL 压入返回地址）
    // 函数体内需 RSP ≡ 0 (mod 16) 以便后续 CALL 对齐
    // RSP_after = entry_RSP - (pushedBytes + alignedFrame)
    // 需要 (pushedBytes + alignedFrame) ≡ 8 (mod 16)
    while ((pushedBytes + alignedFrame) % 16 != 8) alignedFrame += 8;
    // spill 槽位紧接在 callee-saved 保存之后
    spillBase = -pushedBytes;

    if (alignedFrame > 0) {
        emitREX(1, 0, 0, 0);
        emit8(0x81);
        emitModRM(3, 5, RSP & 7);
        emit32(alignedFrame);
    }

    // 将参数从 SysV 调用约定寄存器复制到对应的 callee-saved 物理寄存器
    // （必须在 callee-saved push 之后，确保原始值已保存）
    if (func.paramCount >= 1) emitMOV(RBX, RDI);   // v1 ← RDI
    if (func.paramCount >= 2) emitMOV(R12, RSI);   // v2 ← RSI
    if (func.paramCount >= 3) emitMOV(R13, RDX);   // v3 ← RDX
    if (func.paramCount >= 4) emitMOV(R14, RCX);   // v4 ← RCX
    if (func.paramCount >= 5) emitMOV(R15, R8);    // v5 ← R8
    if (func.paramCount >= 6) emitMOV(RSI, R9);    // v6 ← R9

    // ===== 翻译字节码 =====
    vector<int> pushStack; // 记录 PUSH 的虚拟寄存器，供 CALL 使用

    for (int pc = func.codeOffset; pc < endOff; pc += 8) {
        // 无条件记录每个字节码 PC 的 JIT 偏移，确保后向跳转总能找到目标
        pcLabels[pc] = (int32_t)codePos;

        Opcode op = (Opcode)code[pc];
        uint8_t rd = code[pc + 1];
        uint8_t rs1 = code[pc + 2];
        uint8_t rs2 = code[pc + 3];
        int extra;
        memcpy(&extra, &code[pc + 4], 4);

        switch (op) {
        case Opcode::MOVI: {
            int pdst = vregToPhys(rd);
            if (pdst >= 0)
                emitMOVi(pdst, constants[extra]);
            else {
                // spill: 加载到临时寄存器然后存入栈
                int tmp = R10;
                emitMOVi(tmp, constants[extra]);
                emitMOVtoStack(spillBase - (rd - 14) * 8, tmp);
            }

#ifdef _DEBUG
            {
                const char* names[] = { "RAX", "RCX", "RDX", "RBX",
                                        "RSP", "RBP", "RSI", "RDI",
                                        "R8",  "R9",  "R10", "R11",
                                        "R12", "R13", "R14", "R15" };
                if (pdst >= 0)
                    std::cerr << "[JIT]   MOVI v" << rd << " (" << names[pdst]
                              << "), #" << constants[extra] << "\n";
                else
                    std::cerr << "[JIT]   MOVI v" << rd << " (栈[rbp"
                              << (spillBase - (rd - 14) * 8) << "]), #"
                              << constants[extra] << "\n";
            }
#endif

            break;
        }

        case Opcode::MOV: {
            int pdst = vregToPhys(rd);
            int psrc = vregToPhys(rs1);
            if (pdst >= 0 && psrc >= 0)
                emitMOV(pdst, psrc);
            else if (pdst >= 0) {
                // 从栈加载到寄存
                emitMOVfromStack(pdst, spillBase - (rs1 - 14) * 8);
            } else if (psrc >= 0) {
                // 从寄存存入栈
                emitMOVtoStack(spillBase - (rd - 14) * 8, psrc);
            } else {
                // 栈到栈：经临时寄存器中转
                int tmp = R10;
                emitMOVfromStack(tmp, spillBase - (rs1 - 14) * 8);
                emitMOVtoStack(spillBase - (rd - 14) * 8, tmp);
            }

#ifdef _DEBUG
            {
                const char* names[] = { "RAX", "RCX", "RDX", "RBX",
                                        "RSP", "RBP", "RSI", "RDI",
                                        "R8",  "R9",  "R10", "R11",
                                        "R12", "R13", "R14", "R15" };
                if (pdst >= 0 && psrc >= 0)
                    std::cerr << "[JIT]   MOV v" << rd << " (" << names[pdst]
                              << "), v" << rs1 << " (" << names[psrc] << ")\n";
                else if (pdst >= 0)
                    std::cerr << "[JIT]   MOV v" << rd << " (" << names[pdst]
                              << "), v" << rs1 << " (栈[rbp"
                              << (spillBase - (rs1 - 14) * 8) << "])\n";
                else if (psrc >= 0)
                    std::cerr << "[JIT]   MOV v" << rd << " (栈[rbp"
                              << (spillBase - (rd - 14) * 8) << "]), v" << rs1
                              << " (" << names[psrc] << ")\n";
                else
                    std::cerr << "[JIT]   MOV v" << rd << " (栈[rbp"
                              << (spillBase - (rd - 14) * 8) << "]), v" << rs1
                              << " (栈[rbp" << (spillBase - (rs1 - 14) * 8)
                              << "])\n";
            }
#endif

            break;
        }

        case Opcode::ADD: {
            int pdst = vregToPhys(rd);
            int psrc1 = vregToPhys(rs1);
            int psrc2 = vregToPhys(rs2);
            if (pdst >= 0 && psrc1 >= 0 && psrc2 >= 0) {
                if (pdst != psrc1) emitMOV(pdst, psrc1);
                emitADD(pdst, psrc2);
            }
            // 其他情况简化处理：用 R10 中转
            else {
                int tmp = R10;
                if (psrc1 >= 0)
                    emitMOV(tmp, psrc1);
                else
                    emitMOVfromStack(tmp, spillBase - (rs1 - 14) * 8);
                if (psrc2 >= 0)
                    emitADD(tmp, psrc2);
                else {
                    int t2 = R11;
                    emitMOVfromStack(t2, spillBase - (rs2 - 14) * 8);
                    emitADD(tmp, t2);
                }
                if (pdst >= 0)
                    emitMOV(pdst, tmp);
                else
                    emitMOVtoStack(spillBase - (rd - 14) * 8, tmp);
            }

#ifdef _DEBUG
            {
                const char* names[] = { "RAX", "RCX", "RDX", "RBX",
                                        "RSP", "RBP", "RSI", "RDI",
                                        "R8",  "R9",  "R10", "R11",
                                        "R12", "R13", "R14", "R15" };
                std::cerr << "[JIT]   ADD v" << rd << ", v" << rs1 << ", v"
                          << rs2;
                if (pdst >= 0) std::cerr << " -> " << names[pdst];
                std::cerr << "\n";
            }
#endif

            break;
        }

        case Opcode::SUB: {
            int pdst = vregToPhys(rd);
            int psrc1 = vregToPhys(rs1);
            int psrc2 = vregToPhys(rs2);
            if (pdst >= 0 && psrc1 >= 0 && psrc2 >= 0) {
                if (pdst != psrc1) emitMOV(pdst, psrc1);
                emitSUB(pdst, psrc2);
            } else {
                int tmp = R10;
                if (psrc1 >= 0)
                    emitMOV(tmp, psrc1);
                else
                    emitMOVfromStack(tmp, spillBase - (rs1 - 14) * 8);
                if (psrc2 >= 0)
                    emitSUB(tmp, psrc2);
                else {
                    int t2 = R11;
                    emitMOVfromStack(t2, spillBase - (rs2 - 14) * 8);
                    emitSUB(tmp, t2);
                }
                if (pdst >= 0)
                    emitMOV(pdst, tmp);
                else
                    emitMOVtoStack(spillBase - (rd - 14) * 8, tmp);
            }

#ifdef _DEBUG
            {
                const char* names[] = { "RAX", "RCX", "RDX", "RBX",
                                        "RSP", "RBP", "RSI", "RDI",
                                        "R8",  "R9",  "R10", "R11",
                                        "R12", "R13", "R14", "R15" };
                std::cerr << "[JIT]   SUB v" << rd << ", v" << rs1 << ", v"
                          << rs2;
                if (pdst >= 0) std::cerr << " -> " << names[pdst];
                std::cerr << "\n";
            }
#endif

            break;
        }

        case Opcode::MUL: {
            int pdst = vregToPhys(rd);
            int psrc1 = vregToPhys(rs1);
            int psrc2 = vregToPhys(rs2);
            if (pdst >= 0 && psrc1 >= 0 && psrc2 >= 0) {
                if (pdst != psrc1) emitMOV(pdst, psrc1);
                emitIMUL(pdst, psrc2);
            } else {
                int tmp = R10;
                if (psrc1 >= 0)
                    emitMOV(tmp, psrc1);
                else
                    emitMOVfromStack(tmp, spillBase - (rs1 - 14) * 8);
                if (psrc2 >= 0)
                    emitIMUL(tmp, psrc2);
                else {
                    int t2 = R11;
                    emitMOVfromStack(t2, spillBase - (rs2 - 14) * 8);
                    emitIMUL(tmp, t2);
                }
                if (pdst >= 0)
                    emitMOV(pdst, tmp);
                else
                    emitMOVtoStack(spillBase - (rd - 14) * 8, tmp);
            }

#ifdef _DEBUG
            {
                const char* names[] = { "RAX", "RCX", "RDX", "RBX",
                                        "RSP", "RBP", "RSI", "RDI",
                                        "R8",  "R9",  "R10", "R11",
                                        "R12", "R13", "R14", "R15" };
                std::cerr << "[JIT]   MUL v" << rd << ", v" << rs1 << ", v"
                          << rs2;
                if (pdst >= 0) std::cerr << " -> " << names[pdst];
                std::cerr << "\n";
            }
#endif

            break;
        }

        case Opcode::DIV: {
            int pdst = vregToPhys(rd);
            int psrc1 = vregToPhys(rs1);
            int psrc2 = vregToPhys(rs2);
            if (psrc1 >= 0) {
                if (psrc1 != RAX) emitMOV(RAX, psrc1);
            } else {
                emitMOVfromStack(RAX, spillBase - (rs1 - 14) * 8);
            }
            emitREX(1, 0, 0, 0);
            emit8(0x99);
            if (psrc2 >= 0) {
                emitREX(1, 0, 0, psrc2 >= 8);
                emit8(0xF7);
                emitModRM(3, 7, psrc2 & 7);
            } else {
                int t2 = R10;
                emitMOVfromStack(t2, spillBase - (rs2 - 14) * 8);
                emitREX(1, 0, 0, 0);
                emit8(0xF7);
                emitModRM(3, 7, t2 & 7);
            }
            if (pdst >= 0 && pdst != RAX)
                emitMOV(pdst, RAX);
            else if (pdst < 0)
                emitMOVtoStack(spillBase - (rd - 14) * 8, RAX);
            break;
        }

        case Opcode::MOD: {
            int pdst = vregToPhys(rd);
            int psrc1 = vregToPhys(rs1);
            int psrc2 = vregToPhys(rs2);
            if (psrc1 >= 0) {
                if (psrc1 != RAX) emitMOV(RAX, psrc1);
            } else {
                emitMOVfromStack(RAX, spillBase - (rs1 - 14) * 8);
            }
            emitREX(1, 0, 0, 0);
            emit8(0x99);
            if (psrc2 >= 0) {
                emitREX(1, 0, 0, psrc2 >= 8);
                emit8(0xF7);
                emitModRM(3, 7, psrc2 & 7);
            } else {
                int t2 = R10;
                emitMOVfromStack(t2, spillBase - (rs2 - 14) * 8);
                emitREX(1, 0, 0, 0);
                emit8(0xF7);
                emitModRM(3, 7, t2 & 7);
            }
            if (pdst >= 0 && pdst != RDX)
                emitMOV(pdst, RDX);
            else if (pdst < 0)
                emitMOVtoStack(spillBase - (rd - 14) * 8, RDX);
            break;
        }

        case Opcode::EQ: {
            int pdst = vregToPhys(rd);
            int psrc1 = vregToPhys(rs1);
            int psrc2 = vregToPhys(rs2);
            if (psrc1 >= 0 && psrc2 >= 0)
                emitCMP(psrc1, psrc2);
            else {
                int t1 = R10, t2 = R11;
                if (psrc1 >= 0)
                    emitMOV(t1, psrc1);
                else
                    emitMOVfromStack(t1, spillBase - (rs1 - 14) * 8);
                if (psrc2 >= 0)
                    emitMOV(t2, psrc2);
                else
                    emitMOVfromStack(t2, spillBase - (rs2 - 14) * 8);
                emitCMP(t1, t2);
            }
            int pbyte = (pdst >= 0) ? pdst : R10;
            emitSETcc(pbyte, "sete");
            emitMOVZX(pbyte, pbyte);
            if (pdst < 0) emitMOVtoStack(spillBase - (rd - 14) * 8, pbyte);

            break;
        }

        case Opcode::NE: {
            int pdst = vregToPhys(rd);
            int psrc1 = vregToPhys(rs1);
            int psrc2 = vregToPhys(rs2);
            if (psrc1 >= 0 && psrc2 >= 0)
                emitCMP(psrc1, psrc2);
            else {
                int t1 = R10, t2 = R11;
                if (psrc1 >= 0)
                    emitMOV(t1, psrc1);
                else
                    emitMOVfromStack(t1, spillBase - (rs1 - 14) * 8);
                if (psrc2 >= 0)
                    emitMOV(t2, psrc2);
                else
                    emitMOVfromStack(t2, spillBase - (rs2 - 14) * 8);
                emitCMP(t1, t2);
            }
            int pbyte = (pdst >= 0) ? pdst : R10;
            emitSETcc(pbyte, "setne");
            emitMOVZX(pbyte, pbyte);
            if (pdst < 0) emitMOVtoStack(spillBase - (rd - 14) * 8, pbyte);

            break;
        }

        case Opcode::LT: {
            int pdst = vregToPhys(rd);
            int psrc1 = vregToPhys(rs1);
            int psrc2 = vregToPhys(rs2);
            if (psrc1 >= 0 && psrc2 >= 0)
                emitCMP(psrc1, psrc2);
            else {
                int t1 = R10, t2 = R11;
                if (psrc1 >= 0)
                    emitMOV(t1, psrc1);
                else
                    emitMOVfromStack(t1, spillBase - (rs1 - 14) * 8);
                if (psrc2 >= 0)
                    emitMOV(t2, psrc2);
                else
                    emitMOVfromStack(t2, spillBase - (rs2 - 14) * 8);
                emitCMP(t1, t2);
            }
            int pbyte = (pdst >= 0) ? pdst : R10;
            emitSETcc(pbyte, "setl");
            emitMOVZX(pbyte, pbyte);
            if (pdst < 0) emitMOVtoStack(spillBase - (rd - 14) * 8, pbyte);

            break;
        }

        case Opcode::GT: {
            int pdst = vregToPhys(rd);
            int psrc1 = vregToPhys(rs1);
            int psrc2 = vregToPhys(rs2);
            if (psrc1 >= 0 && psrc2 >= 0)
                emitCMP(psrc1, psrc2);
            else {
                int t1 = R10, t2 = R11;
                if (psrc1 >= 0)
                    emitMOV(t1, psrc1);
                else
                    emitMOVfromStack(t1, spillBase - (rs1 - 14) * 8);
                if (psrc2 >= 0)
                    emitMOV(t2, psrc2);
                else
                    emitMOVfromStack(t2, spillBase - (rs2 - 14) * 8);
                emitCMP(t1, t2);
            }
            int pbyte = (pdst >= 0) ? pdst : R10;
            emitSETcc(pbyte, "setg");
            emitMOVZX(pbyte, pbyte);
            if (pdst < 0) emitMOVtoStack(spillBase - (rd - 14) * 8, pbyte);

            break;
        }

        case Opcode::LE: {
            int pdst = vregToPhys(rd);
            int psrc1 = vregToPhys(rs1);
            int psrc2 = vregToPhys(rs2);
            if (psrc1 >= 0 && psrc2 >= 0)
                emitCMP(psrc1, psrc2);
            else {
                int t1 = R10, t2 = R11;
                if (psrc1 >= 0)
                    emitMOV(t1, psrc1);
                else
                    emitMOVfromStack(t1, spillBase - (rs1 - 14) * 8);
                if (psrc2 >= 0)
                    emitMOV(t2, psrc2);
                else
                    emitMOVfromStack(t2, spillBase - (rs2 - 14) * 8);
                emitCMP(t1, t2);
            }
            int pbyte = (pdst >= 0) ? pdst : R10;
            emitSETcc(pbyte, "setle");
            emitMOVZX(pbyte, pbyte);
            if (pdst < 0) emitMOVtoStack(spillBase - (rd - 14) * 8, pbyte);

            break;
        }

        case Opcode::GE: {
            int pdst = vregToPhys(rd);
            int psrc1 = vregToPhys(rs1);
            int psrc2 = vregToPhys(rs2);
            if (psrc1 >= 0 && psrc2 >= 0)
                emitCMP(psrc1, psrc2);
            else {
                int t1 = R10, t2 = R11;
                if (psrc1 >= 0)
                    emitMOV(t1, psrc1);
                else
                    emitMOVfromStack(t1, spillBase - (rs1 - 14) * 8);
                if (psrc2 >= 0)
                    emitMOV(t2, psrc2);
                else
                    emitMOVfromStack(t2, spillBase - (rs2 - 14) * 8);
                emitCMP(t1, t2);
            }
            int pbyte = (pdst >= 0) ? pdst : R10;
            emitSETcc(pbyte, "setge");
            emitMOVZX(pbyte, pbyte);
            if (pdst < 0) emitMOVtoStack(spillBase - (rd - 14) * 8, pbyte);

            break;
        }

        case Opcode::JMP: {
            int targetPc = pc + 8 + extra;
            // 查找是否已有此 PC 的 label
            auto it = pcLabels.find(targetPc);
            if (it != pcLabels.end()) {
                // 后向跳转：label 已绑定
                // JMP rel32 指令长5字节，偏移量从指令末尾算起
                int32_t off = (int32_t)it->second - (int32_t)(codePos + 5);
                emitJMPrel(off);
            } else {
                // 前向跳转：创建新 label，稍后回填
                makeLabel();
                pcLabels[targetPc] = (int32_t)-1; // 占位
                size_t patchPos = codePos;
                emit8(0xE9);
                emit32(0); // jmp rel32 占位
                forwardPatches.push_back({ patchPos, targetPc });
            }

            break;
        }

        case Opcode::JIF: {
            int psrc1 = vregToPhys(rs1);
            if (psrc1 >= 0)
                emitTEST(psrc1);
            else {
                int tmp = R10;
                emitMOVfromStack(tmp, spillBase - (rs1 - 14) * 8);
                emitTEST(tmp);
            }

            int targetPc = pc + 8 + extra;
            auto it = pcLabels.find(targetPc);
            if (it != pcLabels.end()) {
                // 后向跳转
                int32_t off = (int32_t)it->second - (int32_t)(codePos + 6);
                emit8(0x0F);
                emit8(0x84);
                emit32(off); // je rel32
            } else {
                // 前向跳转
                pcLabels[targetPc] = (int32_t)-1;
                size_t patchPos = codePos;
                emit8(0x0F);
                emit8(0x84);
                emit32(0); // je rel32 占位
                forwardPatches.push_back({ patchPos, targetPc });
            }

            break;
        }

        case Opcode::PUSH: {
            pushStack.push_back(rs1);

            break;
        }

        case Opcode::PRINT: {
            // 将源寄存器的值加载到 RDI（SysV 第一个参数）
            int psrc = vregToPhys(rs1);
            if (psrc >= 0 && psrc != RDI)
                emitMOV(RDI, psrc);
            else if (psrc < 0)
                emitMOVfromStack(RDI, spillBase - (rs1 - 14) * 8);

            // 保存 caller-saved 寄存器（jit_print_int 会破坏它们）
            emitPUSH(R11);
            emitPUSH(R10);
            emitPUSH(R9);
            emitPUSH(R8);
            emitPUSH(RCX);
            emitPUSH(RDX);
            emitPUSH(RDI);
            emitPUSH(RSI);

            // 间接调用 jit_print_int（避免 rel32 溢出）
            emitMOVi64(R10, (int64_t)jit_print_int);
            emitCALLreg(R10);

            // 恢复 caller-saved 寄存器
            emitPOP(RSI);
            emitPOP(RDI);
            emitPOP(RDX);
            emitPOP(RCX);
            emitPOP(R8);
            emitPOP(R9);
            emitPOP(R10);
            emitPOP(R11);

            break;
        }

        case Opcode::CALL: {
            int funcIdx2 = extra;
            int pCnt = prog.functions[funcIdx2].paramCount;

            // 跳过 PUSH 中的 dummy（前 pushes - pCnt 个是 dummy/开销）
            int totalPushes = (int)pushStack.size();
            int realStart = totalPushes - pCnt;
            if (realStart < 0) realStart = 0;

#ifdef _DEBUG
            std::cerr << "[JIT]   CALL func[" << funcIdx2 << "] "
                      << prog.functions[funcIdx2].name << " 参数数=" << pCnt
                      << " 栈中vregs=" << totalPushes << "\n";
            std::cerr << "[JIT]     caller-saved保存: push "
                         "R11,R10,R9,R8,RCX,RDX,RDI,RSI (8寄存器)\n";
#endif

            // 保存所有 caller-saved 寄存器（v6-v13 =
            // RSI/RDI/RDX/RCX/R8/R9/R10/R11） 这些寄存器在 SysV ABI 中是
            // caller-saved，被调用者会破坏
            emitPUSH(R11); // v13
            emitPUSH(R10); // v12
            emitPUSH(R9);  // v11
            emitPUSH(R8);  // v10
            emitPUSH(RCX); // v9
            emitPUSH(RDX); // v8
            emitPUSH(RDI); // v7
            emitPUSH(RSI); // v6

            // 参数寄存器列表：rdi, rsi, rdx, rcx, r8, r9
            static const uint8_t argRegs[] = { RDI, RSI, RDX, RCX, R8, R9 };
            const char* argRegNames[]
                = { "RDI", "RSI", "RDX", "RCX", "R8", "R9" };
            for (int i = 0; i < pCnt && i < 6; i++) {
                int vreg = pushStack[realStart + i];
                int preg = vregToPhys(vreg);
                if (preg >= 0) {
                    if (preg == argRegs[i]) {
                        // 源和目标相同，不需要拷贝
                    } else if (preg == RDI || preg == RSI || preg == RDX
                               || preg == RCX || preg == R8 || preg == R9) {
                        // 源是参数寄存器之一：上文已把全部 caller-saved
                        // 寄存器 push 到栈上保存原始值，从保存槽读取，
                        // 避免先前参数装载（如 mov rsi,r13）覆盖了它
                        // （装载循环只写 RDI/RSI/RDX/RCX/R8/R9）
                        // push 顺序: R11,R10,R9,R8,RCX,RDX,RDI,RSI
                        // → 最后一个 push 落在 [rsp+0]，按寄存器编号的
                        //   RSP 相对偏移:
                        //   RCX=24 RDX=16 RSI=0 RDI=8 R8=32 R9=40
                        //   R10=48 R11=56
                        static const int8_t savedOff[]
                            = { 0,  24, 16, 0,  0,  0,  0,  8,
                                32, 40, 48, 56, 0,  0 };
                        emitMOVfromRSP(argRegs[i], savedOff[preg]);
                    } else {
                        emitMOV(argRegs[i], preg);
                    }
                } else {
                    emitMOVfromStack(argRegs[i], spillBase - (vreg - 14) * 8);
                }
#ifdef _DEBUG
                const char* names[] = { "RAX", "RCX", "RDX", "RBX",
                                        "RSP", "RBP", "RSI", "RDI",
                                        "R8",  "R9",  "R10", "R11",
                                        "R12", "R13", "R14", "R15" };
                if (preg >= 0)
                    std::cerr << "[JIT]     arg" << i << ": v" << vreg << " -> "
                              << names[preg] << " -> " << argRegNames[i]
                              << "\n";
                else
                    std::cerr << "[JIT]     arg" << i << ": v" << vreg
                              << " -> [rbp" << (spillBase - (vreg - 14) * 8)
                              << "] -> " << argRegNames[i] << "\n";
#endif
            }
            pushStack.clear();

            // 调用目标 JIT 函数：通过全局表 g_jitTable 间接调用
            // 1. rax = g_jitTable 基址
            emitMOVi64(RAX, (int64_t)g_jitTable);
            // 2. 加上 funcIdx * 8 偏移
            if (funcIdx2 > 0) {
                emitMOVi64(R10, funcIdx2 * 8);
                emitADD(RAX, R10);
            }
            // 3. rax = g_jitTable[funcIdx2] (函数指针)
            emitREX(1, 0, 0, 0);
            emit8(0x8B);
            emitModRM(0, RAX & 7, RAX & 7);
            // mov rax, [rax]  (无 displacement)
            // 4. 间接调用
            emitCALLreg(RAX);

            // 恢复 caller-saved 寄存器
            emitPOP(RSI); // v6
            emitPOP(RDI); // v7
            emitPOP(RDX); // v8
            emitPOP(RCX); // v9
            emitPOP(R8);  // v10
            emitPOP(R9);  // v11
            emitPOP(R10); // v12
            emitPOP(R11); // v13

#ifdef _DEBUG
            std::cerr << "[JIT]     caller-saved恢复: pop "
                         "RSI,RDI,RDX,RCX,R8,R9,R10,R11\n";
#endif

            // 结果在 RAX → 写入目的地
            int pdst = vregToPhys(rd);
            if (pdst >= 0 && pdst != RAX)
                emitMOV(pdst, RAX);
            else if (pdst < 0)
                emitMOVtoStack(spillBase - (rd - 14) * 8, RAX);

#ifdef _DEBUG
            {
                const char* names[] = { "RAX", "RCX", "RDX", "RBX",
                                        "RSP", "RBP", "RSI", "RDI",
                                        "R8",  "R9",  "R10", "R11",
                                        "R12", "R13", "R14", "R15" };
                if (pdst >= 0)
                    std::cerr << "[JIT]     结果: RAX -> v" << rd << " ("
                              << names[pdst] << ")\n";
                else
                    std::cerr << "[JIT]     结果: RAX -> v" << rd << " (栈[rbp"
                              << (spillBase - (rd - 14) * 8) << "])\n";
            }
#endif

            break;
        }

        case Opcode::RET: {
            // 返回值已经在 RAX（v0）
            // 发射 JMP 到尾声（func_done），稍后回填
            skipEpilogue.push_back(codePos);
            emit8(0xE9);
            emit32(0);
            break;
        }

        case Opcode::HALT:
            skipEpilogue.push_back(codePos);
            emit8(0xE9);
            emit32(0);
            break;

        default:
            // 不支持的指令：NOP 并继续
            emitNOP();

            break;
        }
    }

    // ===== 函数尾声 =====
    int32_t epilogueStart = (int32_t)codePos;
#ifdef _DEBUG
    std::cerr << "[JIT]   epilogue @ offset " << codePos
              << " (函数大小=" << (codePos - funcStart) << " 字节)\n";
    std::cerr << "[JIT]   spillBase=" << spillBase << " 对齐帧=" << alignedFrame
              << "\n";
    std::cerr << "[JIT]   编译完成: " << (codePos - funcStart)
              << " 字节机器码\n";
#endif

    if (alignedFrame > 0) {
        emitREX(1, 0, 0, 0);
        emit8(0x81);
        emitModRM(3, 0, RSP & 7);
        emit32(alignedFrame);
    }
    if (needR15) emitPOP(R15);
    if (needR14) emitPOP(R14);
    if (needR13) emitPOP(R13);
    if (needR12) emitPOP(R12);
    if (needRBX) emitPOP(RBX);
    emitPOP(RBP);
    emitRET();

    // 回填所有前向跳转
    for (auto& pe : forwardPatches) {
        int32_t targetJIT = pcLabels[pe.targetPc];
        uint8_t opc = codeBuf[pe.codePos];
        int instrLen = (opc == 0xE9) ? 5 : 6;
        int32_t offset = targetJIT - (int32_t)(pe.codePos + instrLen);
        memcpy(codeBuf + pe.codePos + instrLen - 4, &offset, 4);
    }

    // 回填 epilogue 跳转（指向 func_done 处）
    for (size_t pos : skipEpilogue) {
        int32_t offset = epilogueStart - (int32_t)(pos + 5);
        memcpy(codeBuf + pos + 1, &offset, 4);
    }

    return (JITFunc)(codeBuf + funcStart);
}

void JITCompiler::compile(BytecodeProgram& prog)
{
    // 初始化全局表
    g_jitTableSize = (int)prog.functions.size();
    for (int i = 0; i < g_jitTableSize; i++) g_jitTable[i] = 0;

#ifdef _DEBUG
    std::cerr << "\n[JIT] ======== JIT 编译开始 ========\n";
    std::cerr << "[JIT] 函数总数=" << g_jitTableSize << "\n";
#endif

    // 第一遍：为每个可 JIT 的函数分配代码槽位
    // 使 g_jitTable 中的指针可以被递归调用提前引用
    for (size_t i = 0; i < prog.functions.size(); i++) {
        if (canJIT(prog.functions[i], prog)) {
            // 先放一个非空占位（值不重要，后续会覆盖）
            g_jitTable[i] = (int64_t)1;
        }
    }

    // 第二遍：实际编译
    for (size_t i = 0; i < prog.functions.size(); i++) {
        if (g_jitTable[i]) {
            size_t savedPos = codePos;

            JITFunc fn = compileFunction((int)i, prog.functions[i], prog);
            int64_t fnAddr = (int64_t)fn;
            g_jitTable[i] = fnAddr;
            prog.functions[i].jitFunc = (void*)fnAddr;

#ifdef _DEBUG
            std::cerr << "[JIT]   函数[" << i << "] " << prog.functions[i].name
                      << " -> 地址=" << (void*)fnAddr
                      << " 大小=" << (codePos - savedPos) << " 字节\n";
#endif

            // 编译完成，设置只读 + 执行权限
            if (savedPos < codePos) {
                size_t ps = arch::getPageSize();
                size_t pageStart = savedPos & ~(ps - 1);
                size_t pageEnd = (codePos + ps - 1) & ~(ps - 1);
                arch::protectExecutableMemory(codeBuf + pageStart,
                                              pageEnd - pageStart,
                                              7); // RWX
            }
        }
    }

#ifdef _DEBUG
    std::cerr << "[JIT] ======== JIT 编译完成 ========\n";
    std::cerr << "[JIT] 总机器码大小=" << codePos << " 字节\n";
    std::cerr << "[JIT] g_jitTable 内容:\n";
    for (int i = 0; i < g_jitTableSize; i++) {
        if (g_jitTable[i])
            std::cerr << "   [" << i << "] " << prog.functions[i].name << " = "
                      << (void*)g_jitTable[i] << "\n";
        else
            std::cerr << "   [" << i << "] " << prog.functions[i].name
                      << " = (解释器)\n";
    }
    std::cerr << "[JIT] ===================================\n\n";
#endif
}

#endif // OPTIMIZATION
