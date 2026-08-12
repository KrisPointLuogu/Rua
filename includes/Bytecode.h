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
    ARRNEW = 0x15, // rd = 新建长度为 rs1 的数组，元素全部初始化为 rs2
    ARRGET = 0x16, // rd = 数组[rs1][rs2]（rs1=数组句柄，rs2=索引）
    ARRSET = 0x17, // 数组[rs1][rs2] = rd（rs1=句柄，rs2=索引，rd=新值）
    ARRDIMSET = 0x18, // 数组对象 dims[extra] = rs2（rs1=句柄，rs2=维度长度）
    ARRGETN = 0x19,   // rd = 数组[rs1][栈下标...]，下标个数=extra（变长下标读取）
    ARRSETN = 0x1A,   // 数组[rs1][栈下标...] = rd，下标个数=extra（变长下标写入）
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
    int visit(ArrayDecl& node) override;
    int visit(ArrayLiteral& node) override;
    int visit(IfStmt& node) override;
    int visit(WhileStmt& node) override;
    int visit(ReturnStmt& node) override;
    int visit(ExprStmt& node) override;
    int visit(BinaryExpr& node) override;
    int visit(CallExpr& node) override;
    int visit(IndexExpr& node) override;
    int visit(NumberLiteral& node) override;
    int visit(StringLiteral& node) override;
    int visit(Identifier& node) override;

  private:
    void enterScope();
    void exitScope();
    int allocReg(const std::string& name);
    int lookupReg(const std::string& name);
    int allocTemp();

    // 常量折叠：长度表达式仅限数字字面量 + 算术运算
    bool 折叠常量表达式(const ASTNode* node, int& out);
    // 将初始化列表展平为 (平铺偏移, 表达式) 对（嵌套时按 row-major 定位）
    struct 初始化项 {
        int offset;
        ASTNode* expr;
    };
    void 展平初始化(ArrayLiteral* lit, const std::vector<int>& constDims,
                   int level, int baseOffset, std::vector<初始化项>& out);
    // 收集索引链（外层→内层），返回链长
    int 收集索引链(IndexExpr& node, std::vector<IndexExpr*>& out);
};
