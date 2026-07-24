# Rua 语言

Rua 是一門全中文关键字的编程语言，支持脚本模式（无需入口函数）和传统函数模式。

\*\*词法分析 → 语法分析 → 语义分析 → 字节码生成 → 虚拟机执行

---

## 一、语法

### 1.1 关键字

| 关键字 | 用途          | 说明                          |
| ------ | ------------- | ----------------------------- |
| `喵`   | 函数定义      | `喵 函数名(参数) { 语句 }`    |
| `变量` | 变量声明      | `变量 名 = 表达式`            |
| `如果` | 条件分支      | `如果 条件 那么 { } 否则 { }` |
| `那么` | 条件/循环引导 | 可选，接在 `如果`/`当` 之后   |
| `否则` | 否则分支      | 接在 `如果` 块之后            |
| `当`   | 循环          | `当 条件 那么 { }`            |
| `返回` | 函数返回      | `返回 表达式`                 |
| `喵叫` | 内置输出      | `喵叫(值1, 值2, ...)`         |
| `运行` | 保留          | 用于 REPL 触发执行            |
| `数组` | 保留          | 数组类型（预留）              |
| `或者` | 保留          | 逻辑或（预留）                |

### 1.2 字面量

```
整数:   42
字符串: "hello"  'hello'  “你好”  ‘你好’
```

字符串支持转义：`\n` `\t` `\r` `\\` `\"` `\'`

### 1.3 运算符

```
算术:   +   -   *   /   %   **   //
比较:   ==   !=   >   <   >=   <=
赋值:   =
复合:   +=   -=   *=   /=   %=   **=   //=
分组:   (   )
```

运算符优先级（从低到高）：

```
赋值:       =
比较:       ==  !=  >  <  >=  <=
加减:       +  -
乘除模:     *  /  %
```

### 1.4 注释

```
# 单行注释
#* 多行
   注释 *#
```

### 1.5 文法

```
program       = (function | statement)*

function      = "喵" IDENTIFIER "(" [params] ")" block
params        = IDENTIFIER ("," IDENTIFIER)*
block         = "{" statement* "}"

statement     = varDecl | ifStmt | whileStmt | returnStmt | exprStmt
varDecl       = "变量" IDENTIFIER "=" expression
ifStmt        = "如果" expression "那么"? block ("否则" block)?
whileStmt     = "当" expression "那么"? block
returnStmt    = "返回" expression
exprStmt      = expression

expression    = assignment
assignment    = equality ("=" assignment)?
equality      = comparison (("=="|"!=") comparison)*
comparison    = addition ((">"|"<"|">="|"<=") addition)*
addition      = multiplication (("+"|"-") multiplication)*
multiplication = primary (("*"|"/"|"%") primary)*
primary       = NUMBER | STRING
              | "(" expression ")"
              | IDENTIFIER ("(" [args] ")")?
              | "喵叫" "(" [args] ")"
```

### 1.6 内置函数

| 函数         | 说明                                 |
| ------------ | ------------------------------------ |
| `喵叫(...)`  | 输出参数值（可变参数），自动空格分隔 |
| `运行(路径)` | 运行文件（预留）                     |

---

## 二、文件结构

```
Rua/
├── CMakeLists.txt
├── src/
│   ├── main.cpp              # 入口：文件模式 / REPL 模式
│   ├── REPL.cpp              # REPL 交互 + 编译流水线
│   ├── 词法分析器.cpp         # 词法分析器 (Lexer)
│   ├── 语法分析器.cpp         # 递归下降语法分析器 (Parser)
│   ├── 语义分析.cpp           # 语义分析 + 符号表 (Semantic)
│   ├── 字节码生成.cpp         # 字节码生成 (BytecodeGenerator)
│   ├── 虚拟机.cpp             # 寄存器式虚拟机 (VM)
│   ├── JIT.cpp                # JIT 编译器（x86-64 机器码生成）
│   └── UTF32支持.cpp          # UTF-8 ↔ UTF-32 转换
├── includes/
│   ├── main.h                # 窗口初始化
│   ├── REPL.h                # REPL 接口
│   ├── 词法分析器.h           # Token 定义
│   ├── 语法树.h               # AST 节点定义
│   ├── 语法分析器.h           # Parser 类
│   ├── 语义分析.h             # 语义分析 + 符号表
│   ├── 字节码.h               # 字节码定义 + 生成器
│   ├── 虚拟机.h               # VM 类
│   ├── 全局内容.h             # 全局状态
│   ├── 输出彩色支持.h         # ANSI 彩色输出
│   ├── 异常上报.h             # 错误/警告处理
│   ├── 信息上报.h             # 信息上报
│   ├── 调试输出支持.h         # 调试输出
│   └── 平台检测.h             # 平台检测
├── arch/                       # 平台抽象层
│   ├── JITPlatform.h          # JIT 内存管理接口
│   ├── JITPlatform_linux.h    # Linux: mmap/mprotect
│   ├── JITPlatform_linux.cpp
│   ├── JITPlatform_windows.h  # Windows: VirtualAlloc
│   ├── JITPlatform_windows.cpp
│   ├── Console.h              # 终端控制接口
│   ├── Console_linux.h        # Linux: 终端标题/用户名
│   ├── Console_linux.cpp
│   ├── Console_windows.h      # Windows: Win32 控制台
│   └── Console_windows.cpp
├── scripts/
│   ├── build_debug.py         # Linux Debug
│   ├── build_release.py       # Linux Release
│   ├── build_optimized.py     # Linux 优化版 (JIT)
│   ├── build_debug_windows.py     # Windows Debug
│   ├── build_release_windows.py   # Windows Release
│   └── build_optimized_windows.py # Windows 优化版 (JIT)
├── example/
│   ├── bench_fib.rua          # 斐波那契基准测试
│   ├── bench_loop.rua         # 循环基准测试
│   ├── calc_fib.rua           # 斐波那契计算
│   └── example.rua            # 综合示例
└── docs/
    └── Rua语言参考.md
```

---

## 三、编译流水线详解

### 3.1 词法分析 — `词法分析器`

输入：源码字符串 → 输出：Token 列表

- 源码转换为 UTF-32 后逐字符扫描
- 识别：关键字、标识符、数字、字符串（中/英文引号）、运算符、界符
- 注释：`#` 单行、`#* ... *#` 多行

**添加新关键字：**

```cpp
// 1. 词法分析器.h — 枚举添加
enum 令牌类型 { ..., 新关键字 = N };

// 2. 词法分析器.cpp — 关键词表添加
{U"新词", 新关键字},
```

**添加新运算符：**

```cpp
// 词法分析器.cpp — switch 添加分支
case U'@':
    新令牌(AT, U"@", ...); continue;
```

### 3.2 语法分析 — `语法分析器`

输入：Token 列表 → 输出：AST（`Program`）

- 递归下降解析，每个文法规则对应一个方法
- 新增语句类型：`parseStatement()` 中添加 `if` 分支
- 新增运算符：在对应优先级方法中添加 `case`

### 3.3 语义分析 — `语义分析`

输入：AST → 输出：符号表（副作用：验证 AST）

- 函数定义收集与重复检查
- 变量作用域管理（块级作用域）
- 变量使用前声明检查
- 函数调用参数个数匹配检查
- `返回` 语句必须在函数体内

### 3.4 字节码生成 — `字节码生成`

输入：AST + 符号表 → 输出：`BytecodeProgram`

寄存器式三地址码，每条指令固定 **8 字节**：`opcode(1) + rd(1) + rs1(1) + rs2(1) + extra(4)`。

寄存器分配：r0 保留给返回值，参数从 r1 开始，局部变量依次分配，临时值每句结束后重置。

**添加新指令：**

```cpp
// 1. 字节码.h — Opcode 枚举添加
NEW_INST = 0x13,

// 2. 字节码生成.cpp — visit() 中 emit 新指令

// 3. 虚拟机.cpp — switch 中添加 case
```

### 3.5 虚拟机 — `虚拟机`

寄存器式架构：

- **值栈**：存放所有函数帧，每帧是一组连续虚拟寄存器
- **帧基址（fp）**：当前帧在值栈中的起始位置，`reg(r) = stack[fp + r]`
- **调用栈**：返回地址
- **帧栈**：函数调用帧基址链
- **IP**：指令指针

函数调用约定：

1. 调用者 PUSH r0 占位 → PUSH 参数 → CALL
2. CALL：PUSH 的值成为被调函数的 r0（占位）和 r1..rN（参数），创建新帧
3. RET：被调函数 r0 → 调用者 r0，自动恢复调用者帧
4. 帧栈机制保证调用者寄存器不被破坏

### 3.6 指令集

每条指令固定 8 字节：`opcode(1) + rd(1) + rs1(1) + rs2(1) + extra(4)`

| 操作码 | 指令 | 说明 |
|--------|------|------|
| 0x00 | HALT | 程序终止 |
| 0x01 | MOVI rd, ci | rd = constants[ci] |
| 0x02 | MOVS rd, si | rd = strings[si] |
| 0x03 | MOV rd, rs | 寄存器间复制 |
| 0x04 | ADD rd, rs1, rs2 | rd = rs1 + rs2 |
| 0x05 | SUB rd, rs1, rs2 | rd = rs1 - rs2 |
| 0x06 | MUL rd, rs1, rs2 | rd = rs1 * rs2 |
| 0x07 | DIV rd, rs1, rs2 | rd = rs1 / rs2 |
| 0x08 | MOD rd, rs1, rs2 | rd = rs1 % rs2 |
| 0x09 | EQ rd, rs1, rs2 | rd = (rs1 == rs2) |
| 0x0A | NE rd, rs1, rs2 | rd = (rs1 != rs2) |
| 0x0B | LT rd, rs1, rs2 | rd = (rs1 <  rs2) |
| 0x0C | GT rd, rs1, rs2 | rd = (rs1 >  rs2) |
| 0x0D | JMP offset | ip += offset |
| 0x0E | JIF rs, offset | if (rs == 0) ip += offset |
| 0x0F | PUSH rs | 压栈传参 |
| 0x10 | CALL idx | 调用函数 |
| 0x11 | RET | 函数返回 |
| 0x12 | PRINT rs | 输出 reg(rs) |

### 3.7 JIT 编译 — `JIT`

优化模式 (`-DOPTIMIZATION`) 下，纯整数/算术函数会被编译为 x86-64 机器码直接执行。

**触发条件：**
- 编译时添加 `-DOPTIMIZATION` 宏
- 函数不含 MOVS（字符串操作）或 PRINT（输出）

**寄存器映射 (x86-64)：**

| Rua vreg | x86-64 | 类型 | 说明 |
|----------|--------|------|------|
| v0 | RAX | — | 返回值 |
| v1 | RBX | callee-saved | 参数1 |
| v2 | R12 | callee-saved | — |
| v3 | R13 | callee-saved | — |
| v4 | R14 | callee-saved | — |
| v5 | R15 | callee-saved | — |
| v6 | RSI | caller-saved | — |
| v7 | RDI | caller-saved | — |
| v8 | RDX | caller-saved | — |
| v9 | RCX | caller-saved | — |
| v10 | R8 | caller-saved | — |
| v11 | R9 | caller-saved | — |
| v12 | R10 | caller-saved | 临时 |
| v13 | R11 | caller-saved | 临时 |
| v14+ | 栈 | spill | 溢出到栈 |

**函数调用 (SysV ABI)：**
1. 参数通过 RDI, RSI, RDX, RCX, R8, R9 传递
2. CALL 前保存所有 caller-saved 寄存器
3. CALL 后恢复所有 caller-saved 寄存器
4. 返回值在 RAX

**调试：**
```bash
# 启用 JIT 调试输出
python3 scripts/build_optimized.py  # -DOPTIMIZATION -D_DEBUG
./build/Rua example.rua 2>&1 | grep '\[JIT\]'
```

输出示例：
```
[JIT] ======== JIT 编译开始 ========
[JIT] ========== 编译函数 [0] fib ==========
[JIT]   参数数=1 最大vreg=10
[JIT]   寄存器分配:
[JIT]     v0 -> RAX (caller-saved)
[JIT]     v1 -> RBX (callee-saved)
[JIT]   CALL func[0] fib 参数数=1
[JIT]     caller-saved保存: push R11,R10,R9,R8,RCX,RDX,RDI,RSI
[JIT]   编译完成: 202 字节机器码
[JIT] ======== JIT 编译完成 ========
```

## 四、如何扩展

### 4.1 新增关键词

以添加 `打印` 作为 `喵叫` 的别名为例：

1. **`词法分析器.h`** — 枚举加 `打印关键字 = 46`
2. **`词法分析器.cpp`** — 关键词表加 `{U"打印", 打印关键字}`
3. **`语法分析器.cpp`** — `parsePrimary()` 的 `喵叫` 分支旁加 `打印` 分支
4. **`字节码生成.cpp`** — `visit(CallExpr)` 的 `喵叫` 分支旁加 `打印`

### 4.2 新增语句类型

以添加 `断言` 语句为例：

1. **`语法树.h`** — 新增 `AssertStmt` 节点类 + `ASTVisitor::visit(AssertStmt&)`
2. **`语法分析器.cpp`** — `parseStatement()` 加 `if (check(TK::断言))`，实现 `parseAssertStmt()`
3. **`语义分析.cpp`** — 实现 `visit(AssertStmt&)`
4. **`字节码生成.cpp`** — 实现 `visit(AssertStmt&)`，发射对应字节码
5. **`虚拟机.cpp`** — 若需新指令则在 Opcode 枚举添加 + execute() 加 case

### 4.3 新增运算符

1. **`词法分析器.cpp`** — switch 中添加符号识别
2. **`语法分析器.cpp`** — 对应优先级方法中添加 `case`
3. **`字节码生成.cpp`** — `visit(BinaryExpr)` 中添加 `case`
4. **`字节码.h`** — 若需新指令则添加 Opcode
5. **`虚拟机.cpp`** — execute() 中添加新指令实现

---

## 五、构建与使用

### Linux / macOS

```bash
# Debug 版（解释器模式）
python3 scripts/build_debug.py
./build/Rua example.rua

# Release 版（解释器模式，优化）
python3 scripts/build_release.py
./build/Rua example.rua

# 优化版（JIT + 调试输出）
python3 scripts/build_optimized.py
./build/Rua example.rua   # JIT 编译 + 调试信息
```

### Windows

```bash
# Debug 版（解释器模式）
python scripts/build_debug_windows.py
build_windows\Debug\Rua.exe example.rua

# Release 版（解释器模式，优化）
python scripts/build_release_windows.py
build_windows\Release\Rua.exe example.rua

# 优化版（JIT + 调试输出）
python scripts/build_optimized_windows.py
build_windows\Debug\Rua.exe example.rua   # JIT 编译 + 调试信息
```

### 手动构建

```bash
# Linux Debug（解释器模式）
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/Rua example.rua

# Linux 优化版（JIT 模式）
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-Ofast -Wall -march=native -flto -DOPTIMIZATION -D_DEBUG"
cmake --build build
./build/Rua example.rua   # JIT 编译 + 调试信息

# Windows (需安装 Visual Studio)
cmake -S . -B build_windows -G "Visual Studio 17 2022" -A x64
cmake --build build_windows --config Release
build_windows\Release\Rua.exe example.rua
```

### REPL 模式

```bash
./build/Rua          # 无参数启动 REPL
# 输入多行代码，以 "运行" 结束执行
```
