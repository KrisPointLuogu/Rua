#pragma once
#include "IR/TACInstructions.h"
#include "SemanticAnalysis.h"
#include "AST.h"

class TACGenerator : public ASTVisitor {
public:
    explicit TACGenerator(const SymbolTable& symTable);
    TACProgram generate(Program& ast);

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
    TACProgram program;
    const SymbolTable* symTable;
    std::vector<TACFunction*> funcStack;

    std::vector<std::unordered_map<std::string, int>> regMaps;
    int totalReg = 0;
    int tempReg = 0;
    int maxReg = 0;
    int nextBlockId = 0;
    std::string currentFunction;

    int currentFuncIdx = -1;

    TACFunction* currentFunc();
    void enterScope();
    void exitScope();
    int allocReg(const std::string& name);
    int lookupReg(const std::string& name);
    int allocTemp();
    void emit(std::unique_ptr<TACInst> inst);
    int newBlock();

    // helper to emit TAC instructions into current function
    void emitMovI(TACValue rd, int val);
    void emitMovS(TACValue rd, int strIdx);
    void emitMov(TACValue rd, TACValue rs);
    void emitBinary(TACOpcode op, TACValue rd, TACValue rs1, TACValue rs2);
    void emitJmp(int targetBlock);
    void emitJif(TACValue cond, int targetBlock, int fallBlock);
    void emitParm(TACValue rs);
    void emitCall(TACValue rd, int funcIdx, const std::string& funcName, int argCount);
    void emitPrint(TACValue rs);
    void emitArrayNew(TACValue rd, TACValue size, TACValue init);
    void emitArrayGet(TACValue rd, TACValue arr, TACValue idx);
    void emitArraySet(TACValue val, TACValue arr, TACValue idx);
    void emitArrayDimSet(TACValue arr, int dimIdx, TACValue val);
    void emitArrayGetN(TACValue rd, TACValue arr, int indexCount);
    void emitArraySetN(TACValue val, TACValue arr, int indexCount);
    void emitRet(TACValue rs);
    void emitHalt();
    void emitNop();
    int curIdx();

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