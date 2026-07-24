#include "中间生成.h"
#include <iostream>

namespace {
const int TK_等号 = 21;
const int TK_加号 = 22;
const int TK_减号 = 23;
const int TK_乘号 = 24;
const int TK_除号 = 25;
const int TK_等于 = 26;
const int TK_不等于 = 27;
const int TK_大于 = 28;
const int TK_小于 = 29;
const int TK_模 = 30;
const int TK_大于等于 = 44;
const int TK_小于等于 = 45;
}

TACGenerator::TACGenerator(const SymbolTable& symTable)
    : symTable(&symTable) {}

TACProgram TACGenerator::generate(Program& ast) {
    nextBlockId = 0;
    ast.accept(*this);
    return std::move(program);
}

TACFunction* TACGenerator::currentFunc() {
    return funcStack.empty() ? nullptr : funcStack.back();
}

void TACGenerator::enterScope() { regMaps.emplace_back(); }
void TACGenerator::exitScope() { regMaps.pop_back(); }

int TACGenerator::allocReg(const std::string& name) {
    int reg = totalReg;
    regMaps.back()[name] = reg;
    totalReg++;
    if (reg > maxReg) maxReg = reg;
    return reg;
}

int TACGenerator::lookupReg(const std::string& name) {
    for (int i = static_cast<int>(regMaps.size()) - 1; i >= 0; i--) {
        auto it = regMaps[i].find(name);
        if (it != regMaps[i].end()) return it->second;
    }
    return -1;
}

int TACGenerator::allocTemp() {
    int r = tempReg++;
    if (r > maxReg) maxReg = r;
    return r;
}

void TACGenerator::emit(std::unique_ptr<TACInst> inst) {
    currentFunc()->instructions.push_back(std::move(inst));
}

int TACGenerator::curIdx() {
    return static_cast<int>(currentFunc()->instructions.size());
}

void TACGenerator::emitMovI(TACValue rd, int val) {
    emit(std::make_unique<TACMovI>(rd, val));
}

void TACGenerator::emitMovS(TACValue rd, int strIdx) {
    emit(std::make_unique<TACMovS>(rd, strIdx));
}

void TACGenerator::emitMov(TACValue rd, TACValue rs) {
    emit(std::make_unique<TACMov>(rd, rs));
}

void TACGenerator::emitBinary(TACOpcode op, TACValue rd, TACValue rs1, TACValue rs2) {
    emit(std::make_unique<TACBinary>(op, rd, rs1, rs2));
}

void TACGenerator::emitJmp(int target) {
    emit(std::make_unique<TACJmp>(target));
}

void TACGenerator::emitJif(TACValue cond, int target, int fall) {
    emit(std::make_unique<TACJif>(cond, target, fall));
}

void TACGenerator::emitParm(TACValue rs) {
    emit(std::make_unique<TACParm>(rs));
}

void TACGenerator::emitCall(TACValue rd, int funcIdx, const std::string& funcName, int argCount) {
    emit(std::make_unique<TACCall>(rd, funcIdx, funcName, argCount));
}

void TACGenerator::emitPrint(TACValue rs) {
    emit(std::make_unique<TACPrint>(rs));
}

void TACGenerator::emitRet(TACValue rs) {
    emit(std::make_unique<TACRet>(rs));
}

void TACGenerator::emitHalt() {
    emit(std::make_unique<TACHalt>());
}

int TACGenerator::newBlock() { return curIdx(); }

int TACGenerator::visit(Program& node) {
    if (!node.topLevelStmts.empty()) {
        bool hasExplicitMain = false;
        for (auto& f : node.functions) {
            if (f->name == "主函数") {
                hasExplicitMain = true;
                auto oldBody = std::move(f->body);
                f->body = std::make_unique<Block>();
                for (auto& stmt : node.topLevelStmts)
                    f->body->statements.push_back(std::move(stmt));
                for (auto& stmt : oldBody->statements)
                    f->body->statements.push_back(std::move(stmt));
                break;
            }
        }
        if (!hasExplicitMain) {
            auto mainFunc = std::make_unique<Function>();
            mainFunc->name = "主函数";
            mainFunc->body = std::make_unique<Block>();
            for (auto& stmt : node.topLevelStmts)
                mainFunc->body->statements.push_back(std::move(stmt));
            node.functions.push_back(std::move(mainFunc));
        }
        node.topLevelStmts.clear();
    }

    if (node.functions.empty()) {
        auto mainFunc = std::make_unique<Function>();
        mainFunc->name = "主函数";
        mainFunc->body = std::make_unique<Block>();
        node.functions.push_back(std::move(mainFunc));
    }

    for (auto& func : node.functions)
        program.functions.push_back(TACFunction{
            func->name, static_cast<int>(func->params.size()), 0, 0, {} });

    for (auto& func : node.functions) {
        currentFunction = func->name;
        func->accept(*this);
    }

    for (auto& f : program.functions) {
        f.regCount = maxReg + 1;
        f.localCount = f.regCount - f.paramCount;
    }

    program.entryPoint = "主函数";
    return 0;
}

int TACGenerator::visit(Function& node) {
    int funcIdx = -1;
    for (int i = 0; i < static_cast<int>(program.functions.size()); i++) {
        if (program.functions[i].name == node.name) {
            funcIdx = i;
            break;
        }
    }
    currentFuncIdx = funcIdx;
    funcStack.push_back(&program.functions[funcIdx]);

    totalReg = 0;
    tempReg = 0;
    maxReg = 0;
    regMaps.clear();
    enterScope();

    totalReg = 1;
    tempReg = 1;
    maxReg = 0;
    for (const auto& param : node.params)
        allocReg(param);
    tempReg = totalReg;

    node.body->accept(*this);

    if (currentFunc()->instructions.empty()
        || currentFunc()->instructions.back()->getOpcode() != TACOpcode::RET) {
        emitMovI(TACValue(TACValueKind::TEMP, allocTemp()), 0);
        emitMov(TACValue(TACValueKind::VAR, 0),
                TACValue(TACValueKind::TEMP, tempReg - 1));
        emitRet(TACValue(TACValueKind::VAR, 0));
    }

    program.functions[funcIdx].regCount = maxReg + 1;
    program.functions[funcIdx].localCount = maxReg + 1 - static_cast<int>(node.params.size());

    funcStack.pop_back();
    exitScope();
    return 0;
}

int TACGenerator::visit(Block& node) {
    enterScope();
    for (auto& stmt : node.statements)
        stmt->accept(*this);
    exitScope();
    return 0;
}

int TACGenerator::visit(VarDecl& node) {
    int slot = allocReg(node.name);
    if (node.initializer) {
        tempReg = totalReg;
        int valReg = node.initializer->accept(*this);
        emitMov(TACValue(TACValueKind::VAR, slot),
                TACValue(TACValueKind::TEMP, valReg));
    } else {
        tempReg = totalReg;
        int zeroReg = allocTemp();
        emitMovI(TACValue(TACValueKind::TEMP, zeroReg), 0);
        emitMov(TACValue(TACValueKind::VAR, slot),
                TACValue(TACValueKind::TEMP, zeroReg));
    }
    tempReg = totalReg;
    return slot;
}

int TACGenerator::visit(IfStmt& node) {
    int condReg = node.condition->accept(*this);
    tempReg = totalReg;

    // Emit JIF with placeholder targets, will back-patch
    int jifIdx = curIdx();
    emitJif(TACValue(TACValueKind::TEMP, condReg), -1, -1);

    // Emit then-branch
    node.thenBranch->accept(*this);

    if (node.elseBranch) {
        // Emit JMP to after-else (placeholder)
        int jmpIdx = curIdx();
        emitJmp(-1);

        int elseStart = curIdx();
        node.elseBranch->accept(*this);
        int afterElse = curIdx();

        // Back-patch JIF: if cond==0 goto elseStart, else fall to jifIdx+1
        auto* jif = static_cast<TACJif*>(currentFunc()->instructions[jifIdx].get());
        jif->targetBlock = elseStart;
        jif->fallBlock = jifIdx + 1;

        // Back-patch JMP: goto afterElse
        auto* jmp = static_cast<TACJmp*>(currentFunc()->instructions[jmpIdx].get());
        jmp->targetBlock = afterElse;
    } else {
        int afterThen = curIdx();
        // Back-patch JIF: if cond==0 goto afterThen, else fall to jifIdx+1
        auto* jif = static_cast<TACJif*>(currentFunc()->instructions[jifIdx].get());
        jif->targetBlock = afterThen;
        jif->fallBlock = jifIdx + 1;
    }
    return 0;
}

int TACGenerator::visit(WhileStmt& node) {
    int condStart = curIdx();

    // Emit condition
    int condReg = node.condition->accept(*this);
    tempReg = totalReg;

    // JIF: if cond==0 goto afterLoop, else fall to body
    int jifIdx = curIdx();
    emitJif(TACValue(TACValueKind::TEMP, condReg), -1, -1);

    int bodyStart = curIdx();
    node.body->accept(*this);

    // Jump back to condition
    emitJmp(condStart);

    int afterLoop = curIdx();

    // Back-patch
    auto* jif = static_cast<TACJif*>(currentFunc()->instructions[jifIdx].get());
    jif->targetBlock = afterLoop;
    jif->fallBlock = bodyStart;

    return 0;
}

int TACGenerator::visit(ReturnStmt& node) {
    if (node.value) {
        int valReg = node.value->accept(*this);
        tempReg = totalReg;
        emitMov(TACValue(TACValueKind::VAR, 0),
                TACValue(TACValueKind::TEMP, valReg));
    } else {
        int zeroReg = allocTemp();
        emitMovI(TACValue(TACValueKind::TEMP, zeroReg), 0);
        emitMov(TACValue(TACValueKind::VAR, 0),
                TACValue(TACValueKind::TEMP, zeroReg));
    }
    emitRet(TACValue(TACValueKind::VAR, 0));
    return 0;
}

int TACGenerator::visit(ExprStmt& node) {
    if (!node.expression) return 0;
    node.expression->accept(*this);
    tempReg = totalReg;
    return 0;
}

int TACGenerator::visit(BinaryExpr& node) {
    if (node.op == TK_等号) {
        if (auto* id = dynamic_cast<Identifier*>(node.left.get())) {
            int slot = lookupReg(id->name);
            if (slot < 0) {
                std::cerr << "TAC error: undefined var " << id->name << std::endl;
                return 0;
            }
            int valReg = node.right->accept(*this);
            emitMov(TACValue(TACValueKind::VAR, slot),
                    TACValue(TACValueKind::TEMP, valReg));
            int resultReg = allocTemp();
            emitMov(TACValue(TACValueKind::TEMP, resultReg),
                    TACValue(TACValueKind::VAR, slot));
            return resultReg;
        }
        std::cerr << "TAC error: assignment LHS must be identifier" << std::endl;
        return 0;
    }

    int leftReg = node.left->accept(*this);
    int rightReg = node.right->accept(*this);
    int resultReg = allocTemp();

    TACOpcode op;
    switch (node.op) {
    case TK_加号: op = TACOpcode::ADD; break;
    case TK_减号: op = TACOpcode::SUB; break;
    case TK_乘号: op = TACOpcode::MUL; break;
    case TK_除号: op = TACOpcode::DIV; break;
    case TK_模:   op = TACOpcode::MOD; break;
    case TK_等于: op = TACOpcode::EQ;  break;
    case TK_不等于: op = TACOpcode::NE; break;
    case TK_大于: op = TACOpcode::GT;  break;
    case TK_小于: op = TACOpcode::LT;  break;
    case TK_大于等于: op = TACOpcode::GE; break;
    case TK_小于等于: op = TACOpcode::LE; break;
    default: op = TACOpcode::NOP; break;
    }

    emitBinary(op, TACValue(TACValueKind::TEMP, resultReg),
               TACValue(TACValueKind::TEMP, leftReg),
               TACValue(TACValueKind::TEMP, rightReg));
    return resultReg;
}

int TACGenerator::visit(CallExpr& node) {
    if (node.callee == "喵叫") {
        for (size_t i = 0; i < node.args.size(); i++) {
            int argReg = node.args[i]->accept(*this);
            emitPrint(TACValue(TACValueKind::TEMP, argReg));
        }
        int resultReg = allocTemp();
        emitMovI(TACValue(TACValueKind::TEMP, resultReg), 0);
        return resultReg;
    }

    int funcIdx = -1;
    for (int i = 0; i < static_cast<int>(program.functions.size()); i++) {
        if (program.functions[i].name == node.callee) {
            funcIdx = i;
            break;
        }
    }
    if (funcIdx < 0) {
        std::cerr << "TAC error: undefined function " << node.callee << std::endl;
        int resultReg = allocTemp();
        emitMovI(TACValue(TACValueKind::TEMP, resultReg), 0);
        return resultReg;
    }

    int r0Dummy = allocTemp();
    emitMovI(TACValue(TACValueKind::TEMP, r0Dummy), 0);
    emitParm(TACValue(TACValueKind::TEMP, r0Dummy));

    for (auto& arg : node.args) {
        int argReg = arg->accept(*this);
        emitParm(TACValue(TACValueKind::TEMP, argReg));
    }
    tempReg = maxReg + 1;

    int resultReg = allocTemp();
    emitCall(TACValue(TACValueKind::TEMP, resultReg),
             funcIdx, node.callee, static_cast<int>(node.args.size()));
    return resultReg;
}

int TACGenerator::visit(NumberLiteral& node) {
    int rd = allocTemp();
    emitMovI(TACValue(TACValueKind::TEMP, rd), node.value);
    return rd;
}

int TACGenerator::visit(StringLiteral& node) {
    int rd = allocTemp();
    emitMovS(TACValue(TACValueKind::TEMP, rd), program.addString(node.value));
    return rd;
}

int TACGenerator::visit(Identifier& node) {
    int slot = lookupReg(node.name);
    if (slot >= 0) return slot;
    std::cerr << "TAC error: undefined variable " << node.name << std::endl;
    int rd = allocTemp();
    emitMovI(TACValue(TACValueKind::TEMP, rd), 0);
    return rd;
}