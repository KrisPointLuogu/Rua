#pragma once
#include "IR/中间指令.h"
#include "语义分析.h"
#include "语法树.h"

class TACGenerator : public ASTVisitor {
public:
    explicit TACGenerator(const SymbolTable& symTable);
    TACProgram generate(Program& ast);

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
    void emitRet(TACValue rs);
    void emitHalt();
    void emitNop();
    int curIdx();
};