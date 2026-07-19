# Rua 语言

Rua 是一門全中文关键字的编程语言，支持脚本模式（无需入口函数）和传统函数模式。

**词法分析 → 语法分析 → 语义分析 → 字节码生成 → 虚拟机执行

---

## 一、语法

### 1.1 关键字

| 关键字 | 用途 | 说明 |
|--------|------|------|
| `喵` | 函数定义 | `喵 函数名(参数) { 语句 }` |
| `变量` | 变量声明 | `变量 名 = 表达式` |
| `如果` | 条件分支 | `如果 条件 那么 { } 否则 { }` |
| `那么` | 条件/循环引导 | 可选，接在 `如果`/`当` 之后 |
| `否则` | 否则分支 | 接在 `如果` 块之后 |
| `当` | 循环 | `当 条件 那么 { }` |
| `返回` | 函数返回 | `返回 表达式` |
| `喵叫` | 内置输出 | `喵叫(值1, 值2, ...)` |
| `运行` | 保留 | 用于 REPL 触发执行 |
| `数组` | 保留 | 数组类型（预留） |
| `或者` | 保留 | 逻辑或（预留） |

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

| 函数 | 说明 |
|------|------|
| `喵叫(...)` | 输出参数值（可变参数），自动空格分隔 |
| `运行(路径)` | 运行文件（预留） |

---

## 二、文件结构

```
Rua/
├── CMakeLists.txt
├── Rua/
│   ├── main.cpp              # 入口：文件模式 / REPL 模式
│   ├── main.h                # 窗口初始化
│   ├── REPL.cpp              # REPL 交互 + 编译流水线
│   ├── REPL.h
│   │
│   ├── 词法分析器.cpp/.h      # 词法分析器 (Lexer)
│   ├── 语法树.h               # AST 节点定义
│   ├── 语法分析器.cpp/.h      # 递归下降语法分析器 (Parser)
│   ├── 语义分析.cpp/.h        # 语义分析 + 符号表 (Semantic)
│   ├── 字节码.h               # 字节码定义 + 生成器
│   ├── 字节码生成.cpp          # 字节码生成 (BytecodeGenerator)
│   ├── 虚拟机.cpp/.h          # 栈式虚拟机 (VM)
│   │
│   ├── UTF32支持.cpp/.h       # UTF-8 ↔ UTF-32 转换
│   ├── 全局内容.h             # 全局状态
│   ├── 输出彩色支持.h         # ANSI 彩色输出
│   ├── 异常上报.h             # 错误/警告处理
│   ├── 信息上报.h             # 信息上报
│   ├── 调试输出支持.h         # 调试输出
│   └── 平台检测.h             # 平台检测
├── example.rua
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

栈式虚拟机字节码，每条指令 1 字节操作码 + 可选 4 字节操作数。

**添加新指令：**

```cpp
// 1. 字节码.h — Opcode 枚举添加
NEW_INST = 0x14,

// 2. hasOperand() 判断是否需要操作数

// 3. 字节码生成.cpp — visit() 中 emit 新指令

// 4. 虚拟机.cpp — execute() 中添加 case
```

### 3.5 虚拟机 — `虚拟机`

栈式架构：
- **值栈**：运算和参数传递
- **调用栈**：返回地址
- **帧栈**：函数调用帧基址
- **IP**：指令指针

函数调用约定：
1. 参数由调用者压栈
2. CALL：保存返回地址，建立新帧（基址 + 分配局部变量空间）
3. RET：弹出返回值，回收帧，跳回返回地址
4. LOAD/STORE 通过 帧基址+槽位 访问变量

### 3.6 指令集

| 操作码 | 指令 | 操作数 | 说明 |
|--------|------|--------|------|
| 0x00 | HALT | 无 | 程序终止 |
| 0x01 | ICONST | 常量索引 | 压入整数常量 |
| 0x02 | SCONST | 字符串索引 | 压入字符串常量 |
| 0x03 | ADD | 无 | 弹出两值相加 |
| 0x04 | SUB | 无 | 弹出两值相减 |
| 0x05 | MUL | 无 | 弹出两值相乘 |
| 0x06 | DIV | 无 | 弹出两值相除 |
| 0x07 | EQ | 无 | 弹出两值比较相等 |
| 0x08 | JMP | 偏移量 | 无条件跳转 |
| 0x09 | JIF | 偏移量 | 弹出值，若为 0 则跳转 |
| 0x0A | LOAD | 槽位 | 加载局部变量 |
| 0x0B | STORE | 槽位 | 存入局部变量 |
| 0x0C | CALL | 函数索引 | 调用函数 |
| 0x0D | PRINT | 无 | 弹出栈顶并输出 |
| 0x0E | RET | 无 | 函数返回 |
| 0x0F | POP | 无 | 弹出并丢弃 |
| 0x10 | LT | 无 | 小于比较 |
| 0x11 | GT | 无 | 大于比较 |
| 0x12 | NEQ | 无 | 不等于比较 |
| 0x13 | MOD | 无 | 取模 |

---

(BY JAVA JVM OPCODE)

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

```bash
# 构建
cmake -S . -B build
cmake --build build

# 运行文件（脚本模式 / 函数模式均可）
./build/Rua example.rua

# REPL 模式
./build/Rua
# 输入多行代码，以 "运行" 结束执行

# 调试模式
cmake -S . -B build_dbg -DCMAKE_BUILD_TYPE=Debug
cmake --build build_dbg
./build_dbg/Rua example.rua   # 会输出字节码反汇编
```
