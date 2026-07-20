#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <stdexcept>
#include "字节码.h"

//
// 栈式虚拟机 —— 执行 Rua 字节码
//
// 架构：
//   - 值栈：所有运算都在栈上进行
//   - 调用栈：存储函数调用的返回地址
//   - 帧栈：存储每个函数调用的帧基址
//   - IP：指令指针
//

enum class ValueType
{
    INTEGER,
    STRING
};

struct Value
{
    ValueType type;
    int intVal;
    std::string strVal;

    Value();
    explicit Value(int v);
    explicit Value(const std::string &s);
    void print() const;
};

class VMError : public std::runtime_error
{
public:
    explicit VMError(const std::string &message);
};

class VM
{
private:
    const BytecodeProgram *program;
    std::vector<Value> stack;
    std::vector<int> callStack;
    std::vector<int> frameStack;
    int ip;

public:
    VM();
    void run(const BytecodeProgram &prog);
    int getStackDepth() const;
    int getCallDepth() const;

private:
    Value pop();
    void push(const Value &val);
    Value peek(int offset = 0) const;
};
