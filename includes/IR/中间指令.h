#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

enum class TACOpcode : uint8_t {
    NOP = 0,
    MOVI, MOVS, MOV,
    ADD, SUB, MUL, DIV, MOD,
    EQ, NE, LT, GT, LE, GE,
    JMP, JIF,
    CALL, RET,
    PUSH, PRINT,
    HALT
};

enum class TACValueKind { CONST_INT, VAR, TEMP };

struct TACValue {
    TACValueKind kind;
    int index;

    TACValue() : kind(TACValueKind::CONST_INT), index(0) {}
    TACValue(TACValueKind k, int i) : kind(k), index(i) {}

    bool isConst() const { return kind == TACValueKind::CONST_INT; }
    bool isVar() const { return kind == TACValueKind::VAR; }
    bool isTemp() const { return kind == TACValueKind::TEMP; }
    bool operator==(const TACValue& o) const { return kind == o.kind && index == o.index; }
    bool operator!=(const TACValue& o) const { return !(*this == o); }
};

class TACVisitor;

class TACInst {
public:
    int line = 0;
    int column = 0;
    int blockId = -1;
    virtual ~TACInst() = default;
    virtual TACOpcode getOpcode() const = 0;
    virtual int accept(TACVisitor& v) = 0;
    virtual std::unique_ptr<TACInst> clone() const = 0;
};

// rd = const_val
class TACMovI : public TACInst {
public:
    TACValue rd;
    int constVal;
    TACMovI(TACValue rd, int constVal) : rd(rd), constVal(constVal) {}
    TACOpcode getOpcode() const override { return TACOpcode::MOVI; }
    int accept(TACVisitor& v) override;
    std::unique_ptr<TACInst> clone() const override { return std::make_unique<TACMovI>(rd, constVal); }
};

// rd = string_idx
class TACMovS : public TACInst {
public:
    TACValue rd;
    int stringIdx;
    TACMovS(TACValue rd, int stringIdx) : rd(rd), stringIdx(stringIdx) {}
    TACOpcode getOpcode() const override { return TACOpcode::MOVS; }
    int accept(TACVisitor& v) override;
    std::unique_ptr<TACInst> clone() const override { return std::make_unique<TACMovS>(rd, stringIdx); }
};

// rd = rs
class TACMov : public TACInst {
public:
    TACValue rd, rs;
    TACMov(TACValue rd, TACValue rs) : rd(rd), rs(rs) {}
    TACOpcode getOpcode() const override { return TACOpcode::MOV; }
    int accept(TACVisitor& v) override;
    std::unique_ptr<TACInst> clone() const override { return std::make_unique<TACMov>(rd, rs); }
};

// rd = rs1 op rs2 (arithmetic/comparison)
class TACBinary : public TACInst {
public:
    TACOpcode op;
    TACValue rd, rs1, rs2;
    TACBinary(TACOpcode op, TACValue rd, TACValue rs1, TACValue rs2)
        : op(op), rd(rd), rs1(rs1), rs2(rs2) {}
    TACOpcode getOpcode() const override { return op; }
    int accept(TACVisitor& v) override;
    std::unique_ptr<TACInst> clone() const override { return std::make_unique<TACBinary>(op, rd, rs1, rs2); }
};

// jmp targetBlock
class TACJmp : public TACInst {
public:
    int targetBlock;
    explicit TACJmp(int targetBlock) : targetBlock(targetBlock) {}
    TACOpcode getOpcode() const override { return TACOpcode::JMP; }
    int accept(TACVisitor& v) override;
    std::unique_ptr<TACInst> clone() const override { return std::make_unique<TACJmp>(targetBlock); }
};

// if (cond == 0) jmp targetBlock
class TACJif : public TACInst {
public:
    TACValue cond;
    int targetBlock;
    int fallBlock;
    TACJif(TACValue cond, int targetBlock, int fallBlock)
        : cond(cond), targetBlock(targetBlock), fallBlock(fallBlock) {}
    TACOpcode getOpcode() const override { return TACOpcode::JIF; }
    int accept(TACVisitor& v) override;
    std::unique_ptr<TACInst> clone() const override { return std::make_unique<TACJif>(cond, targetBlock, fallBlock); }
};

// push rs (push argument for call)
class TACParm : public TACInst {
public:
    TACValue rs;
    explicit TACParm(TACValue rs) : rs(rs) {}
    TACOpcode getOpcode() const override { return TACOpcode::PUSH; }
    int accept(TACVisitor& v) override;
    std::unique_ptr<TACInst> clone() const override { return std::make_unique<TACParm>(rs); }
};

// call funcIdx -> result in rd
class TACCall : public TACInst {
public:
    TACValue rd;
    int funcIdx;
    std::string funcName;
    int argCount;
    TACCall(TACValue rd, int funcIdx, const std::string& funcName, int argCount)
        : rd(rd), funcIdx(funcIdx), funcName(funcName), argCount(argCount) {}
    TACOpcode getOpcode() const override { return TACOpcode::CALL; }
    int accept(TACVisitor& v) override;
    std::unique_ptr<TACInst> clone() const override {
        return std::make_unique<TACCall>(rd, funcIdx, funcName, argCount);
    }
};

// print rs
class TACPrint : public TACInst {
public:
    TACValue rs;
    explicit TACPrint(TACValue rs) : rs(rs) {}
    TACOpcode getOpcode() const override { return TACOpcode::PRINT; }
    int accept(TACVisitor& v) override;
    std::unique_ptr<TACInst> clone() const override { return std::make_unique<TACPrint>(rs); }
};

// return rs (rs is the return value register)
class TACRet : public TACInst {
public:
    TACValue rs;
    explicit TACRet(TACValue rs) : rs(rs) {}
    TACOpcode getOpcode() const override { return TACOpcode::RET; }
    int accept(TACVisitor& v) override;
    std::unique_ptr<TACInst> clone() const override { return std::make_unique<TACRet>(rs); }
};

// halt
class TACHalt : public TACInst {
public:
    TACHalt() = default;
    TACOpcode getOpcode() const override { return TACOpcode::HALT; }
    int accept(TACVisitor& v) override;
    std::unique_ptr<TACInst> clone() const override { return std::make_unique<TACHalt>(); }
};

class TACVisitor {
public:
    virtual ~TACVisitor() = default;
    virtual int visit(TACMovI& n) = 0;
    virtual int visit(TACMovS& n) = 0;
    virtual int visit(TACMov& n) = 0;
    virtual int visit(TACBinary& n) = 0;
    virtual int visit(TACJmp& n) = 0;
    virtual int visit(TACJif& n) = 0;
    virtual int visit(TACParm& n) = 0;
    virtual int visit(TACCall& n) = 0;
    virtual int visit(TACPrint& n) = 0;
    virtual int visit(TACRet& n) = 0;
    virtual int visit(TACHalt& n) = 0;
};

// TACFunction: one function's TAC representation
struct TACFunction {
    std::string name;
    int paramCount = 0;
    int localCount = 0;
    int regCount = 0;
    std::vector<std::unique_ptr<TACInst>> instructions;
};

// TACProgram: full program in TAC form
struct TACProgram {
    std::vector<TACFunction> functions;
    std::vector<int> constants;
    std::vector<std::string> strings;
    std::string entryPoint;

    int addConstant(int value);
    int addString(const std::string& value);
};