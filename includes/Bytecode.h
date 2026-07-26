#pragma once
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>
#include "SemanticAnalysis.h"
#include "AST.h"
#ifdef OPTIMIZATION
#include "IR/TACInstructions.h"
#endif

//
// 寄存器式字节码生成 —— 将 AST 编译为三地址码
//
// 指令格式：每条指令固定 8 字节
//   byte[0] = opcode
//   byte[1] = rd
//   byte[2] = rs1
//   byte[3] = rs2
//   byte[4-7] = extra (int32, 小端)
//
// 调用约定：
//   参数通过 PUSH 指令压入值栈，CALL 将它们作为新帧的 r0..r(N-1)
//   RET 将返回值写入调用者的 r0
//   调用者的其他寄存器自动保留（帧栈机制）
//

enum class Opcode : uint8_t {
    HALT = 0x00,
    MOVI = 0x01,  // rd = constants[extra]
    MOVS = 0x02,  // rd = strings[extra]
    MOV = 0x03,   // rd = rs1
    ADD = 0x04,   // rd = rs1 + rs2
    SUB = 0x05,   // rd = rs1 - rs2
    MUL = 0x06,   // rd = rs1 * rs2
    DIV = 0x07,   // rd = rs1 / rs2
    MOD = 0x08,   // rd = rs1 % rs2
    EQ = 0x09,    // rd = (rs1 == rs2) ? 1 : 0
    NE = 0x0A,    // rd = (rs1 != rs2) ? 1 : 0
    LT = 0x0B,    // rd = (rs1 <  rs2) ? 1 : 0
    GT = 0x0C,    // rd = (rs1 >  rs2) ? 1 : 0
    JMP = 0x0D,   // ip += extra
    JIF = 0x0E,   // if (rs1 == 0) ip += extra
    PUSH = 0x0F,  // stack[++sp] = reg(rs1)
    CALL = 0x10,  // 调用 functions[extra]，参数已 PUSH
    RET = 0x11,   // 返回，结果在 r0
    PRINT = 0x12, // 输出 reg(rs1)
    LE = 0x13,    // rd = (rs1 <= rs2) ? 1 : 0
    GE = 0x14,    // rd = (rs1 >= rs2) ? 1 : 0
};

struct FunctionInfo {
    std::string name;
    int paramCount;
    int localCount;
    int regCount;
    int codeOffset;
#ifdef OPTIMIZATION
    void* jitFunc = nullptr; // JIT 编译后的函数指针
#endif
};

class BytecodeProgram {
  public:
    std::vector<uint8_t> code;
    std::vector<int> constants;
    std::vector<std::string> strings;
    std::vector<FunctionInfo> functions;
    std::string entryPoint;

    int addConstant(int value);
    int addString(const std::string& value);
    int addFunction(const std::string& name, int paramCount);
    void emit(Opcode op, int rd = 0, int rs1 = 0, int rs2 = 0, int extra = 0);
    int getCodeSize() const;
    void patchOperand(int offset, int value);
    void print() const;
};

class BytecodeGenerator : public ASTVisitor {
  private:
    BytecodeProgram program;
    const SymbolTable* symTable;

    std::vector<std::unordered_map<std::string, int>> regMaps;
    int totalReg = 0; // 已分配的永久寄存器数（参数 + 局部变量）
    int tempReg = 0; // 下一个临时寄存器
    int maxReg = 0;  // 实际使用的最大寄存器索引
    std::string currentFunction;

    // 当前函数待回填的信息
    int currentFuncIdx = -1;
    bool inCallArg = false;

  public:
    explicit BytecodeGenerator(const SymbolTable& symbolTable);
    BytecodeProgram generate(Program& ast);
#ifdef OPTIMIZATION
    BytecodeProgram generateFromTAC(const TACProgram& tac,
                                    const std::vector<int>& tacRegCounts);
#endif

    int visit(Program& node) override;
    int visit(Function& node) override;
    int visit(Block& node) override;
    int visit(VarDecl& node) override;
    int visit(IfStmt& node) override;
    int visit(WhileStmt& node) override;
    int visit(ReturnStmt& node) override;
    int visit(ExprStmt& node) override;
    int visit(BinaryExpr& node) override;
    int visit(CallExpr& node) override;
    int visit(NumberLiteral& node) override;
    int visit(StringLiteral& node) override;
    int visit(Identifier& node) override;

  private:
    void enterScope();
    void exitScope();
    int allocReg(const std::string& name);
    int lookupReg(const std::string& name);
    int allocTemp();
};
