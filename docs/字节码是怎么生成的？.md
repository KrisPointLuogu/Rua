# 字节码是怎么生成的？

## 输入

已通过语义分析的 AST + 符号表。

## 输出

`BytecodeProgram` — 包含：
- `code`：`vector<uint8_t>`，字节码流
- `constants`：`vector<int>`，整数常量池
- `strings`：`vector<string>`，字符串常量池
- `functions`：`vector<FunctionInfo>`，函数表

## 架构：寄存器式三地址码

每条指令显式指定操作数和目标寄存器：
```
栈式: ICONST 5; ICONST 3; ADD        // 隐式栈操作
寄存器式: MOVI r1, 5; MOVI r2, 3; ADD r3, r1, r2  // 显式操作数
```

**寄存器分配策略：**
- `r0` 保留给返回值（不分配给变量）
- `r1..rN` 参数
- `r(N+1)..` 局部变量
- 临时值从最后一个局部变量之后开始线性分配，每句结束后重置

## 流程

`BytecodeGenerator::generate(Program &ast)` 驱动 visitor 遍历 AST，每个 `visit()` 返回结果所在的寄存器索引。

### 1. `visit(Program)` — 布局

```
addFunction("fib", 1)       // 注册函数到函数表
addFunction("主函数", 0)
JMP 0                       // 占位，稍后 patch
  (fib 函数体)
  (主函数 函数体)
patch JMP → 跳过函数体     // entryPos
PUSH rDummy                 // r0 占位
CALL 主函数
HALT
```

### 2. `visit(Function)` — 函数框架

```
funcInfo.codeOffset = getCodeSize()
totalReg = 1; tempReg = 1   // r0 保留
enterScope()
allocReg(params...)          // 参数从 r1 开始分配
tempReg = totalReg
node.body->accept(*this)     // 生成函数体
MOVI rT, 0; MOV r0, rT; RET // 默认返回值
```

### 3. 语句/表达式 → 指令映射

每个表达式的 visit 返回结果所在寄存器，父节点直接用。

| AST 节点 | 发射的字节码 |
|----------|-------------|
| `NumberLiteral(42)` | `MOVI rT, const_idx` → 返回 rT |
| `Identifier(x)` | 直接返回 x 的寄存器 |
| `a = b` | `... b → rV; MOV rA, rV; MOV rT, rA` |
| `a + b` | `... a → rL; ... b → rR; ADD rT, rL, rR` |
| `a >= b` | `LT rT, rL, rR; MOVI rZ, 0; EQ rR, rT, rZ` |
| `喵叫(x)` | `... x → rX; PRINT rX` |
| `fib(args)` | `PUSH rDummy; ... arg → rA; PUSH rA; CALL fib; MOV rT, r0` |
| `如果 cond 那么 {t} 否则 {e}` | `... cond; JIF rCond, else; (t); JMP after; else: (e); after:` |
| `当 cond 那么 {body}` | `loop: ... cond; JIF rCond, end; (body); JMP loop; end:` |

### 4. 调用约定

```
调用前:
    PUSH rDummy       // r0 占位（被调函数保留给返回值）
    PUSH rArg1        // 参数 1 → 被调函数 r1
    PUSH rArg2        // 参数 2 → 被调函数 r2
    ...
    CALL funcIdx

调用后:
    r0 = 返回值
    调用者的 r1+ 不受影响（帧栈隔离）
```

## 指令编码

每条指令固定 **8 字节**：
- **1 字节** opcode
- **1 字节** rd（目标寄存器）
- **1 字节** rs1（源寄存器 1）
- **1 字节** rs2（源寄存器 2，未用则 0）
- **4 字节** extra（int32 小端序，用于常量索引、跳转偏移等）

## 关键代码

`src/字节码生成.cpp` — `BytecodeGenerator` 类，约 650 行
`includes/字节码.h` — `Opcode` 枚举、`BytecodeProgram`、`FunctionInfo`
