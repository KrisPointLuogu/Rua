#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <stdexcept>
#include <memory>
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
    int data;

    Value() : type(ValueType::INTEGER), data(0) {}
    explicit Value(int v) : type(ValueType::INTEGER), data(v) {}
    Value(int d, ValueType t) : type(t), data(d) {}
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
    static constexpr int STACK_CAP = 65536;
    static constexpr int CALL_CAP = 65536;
    static constexpr int FRAME_CAP = 65536;

    std::unique_ptr<Value[]> stackData;
    std::unique_ptr<int[]> callStackData;
    std::unique_ptr<int[]> frameStackData;

    int sp;
    int cp;
    int fp;

    const BytecodeProgram *program;
    int ip;

public:
    VM();
    void run(const BytecodeProgram &prog);
    int getStackDepth() const { return sp + 1; }
    int getCallDepth() const { return cp + 1; }

private:
    inline Value pop()
    {
        return stackData[sp--];
    }
    inline void push(const Value &val)
    {
        stackData[++sp] = val;
    }
    inline Value peek(int offset = 0) const
    {
        return stackData[sp - offset];
    }
};
