# Rua 解释器（Rust 版）— API 文档

> 本文档描述 `src/` 下 Rust 实现的模块结构、数据结构与编译流水线，
> 以及它与原 C 版（includes/*.h）的对应关系。

## 一、总览：整条流水线

```
        main()                      src/main.rs
          │
          ├──> read_file(path) ───────────── 读取源码（UTF-8）
          │
          ├──> lex_all(src) ──────────────── 词法分析 → Vec<Token>
          │        └──> Lexer::lex_next() ── 逐 Token 扫描（含字符串/注释/关键字）
          │
          ├──> Parser::parse_tokens(toks) ── 语法分析 → (prog, fns)
          │        └──> parse_expr/parse_stmt/parse_block/parse_function 递归下降
          │
          ├──> Runtime::build(&fns) ──────── 预编译可 VM 函数
          │        └──> vm::compile_functions ── 可 VM 判定 + 闭包传播 + 字节码生成
          │
          └──> Runtime::run(&prog) ───────── 解释执行
                   └──> Runtime::exec 递归分发
                        ├──> env_get/env_set/env_bind  变量读写
                        ├──> exec_call                 函数调用
                        │     ├──> vm::invoke          VM 函数
                        │     └──> 树遍历解释          其余函数
                        ├──> apply_binop              二元运算
                        ├──> array_get/array_set      数组读写
                        └──> print_val                喵叫输出
```

错误处理统一走 `RuaError { line, msg }`，由 `main()` 格式化「第 N 行：消息」到
stderr 并 `exit(1)`。

## 二、模块与 C 版对应关系

| Rust 模块 | C 版对应 | 职责 |
| --- | --- | --- |
| `src/ast.rs` | `includes/ast.h` + `includes/utf8.h`(error_at) | `Node` 枚举、`RuaError`、`Result` |
| `src/lexer.rs` | `includes/lexer.h` + `includes/utf8.h` | `TokenKind`/`Token`/`Lexer`（UTF-8 由 `char` 码点迭代承担） |
| `src/parser.rs` | `includes/parser.h` | 递归下降语法分析（优先级爬升） |
| `src/runtime.rs` | `includes/runtime.h` | `Value`/`Env`/`Ctx`、树遍历解释、二元运算 |
| `src/vm.rs` | `includes/jit*.h` | 纯整数快路径栈机字节码 VM（替代 x86-64 JIT） |
| `src/debug.rs` | `includes/debug_process.h` + `includes/bytecode.h` | `--debug`/`--dump` 输出与计时 |
| `src/main.rs` | `rua.c` | CLI 入口 |

## 三、核心数据结构

### `ast::Node`（对应 C 版通用 `struct Node`）
Rust 枚举按类型精确承载字段：

```rust
pub enum Node {
    Num { line, num: i64 },
    Str { line, text: String },
    Ident { line, name },
    Var { line, name, right },
    Array { line, name, len, right },        // 广播初始值
    Assign { line, target, right },
    Index { line, base, index },
    Binary { line, op: TokenKind, left, right },
    Call { line, name, args },
    If { line, cond, then, else_b: Option<Box<Node>> },
    While { line, cond, body },
    Return { line, value: Option<Box<Node>> },
    Block { line, stmts },
    Prog { stmts },
    Func { line, name, params, body },
}
```

`Node::kind()` / `Node::line()` 对应 C 版 `kind`/`line` 字段访问。

### `runtime::Value`（对应 C 版标签联合 `Value`）

```rust
pub enum Value {
    Num(i64),
    Str(Rc<String>),
    Arr(Rc<RefCell<Vec<Value>>>),   // 数组句柄共享 + 原地可变
}
```

数组保持 C 版「引用语义」：赋值、传参只复制句柄，多个变量共享同一份数组。

### `runtime::Env`（对应 C 版作用域链）

```rust
pub struct Env {
    parent: Option<Rc<RefCell<Env>>>,
    bindings: Vec<Binding>,          // (name, Value)
}
```

- `env_bind`：当前作用域绑定新变量；
- `env_get`：沿链向上查找（找不到报「未定义的变量」）；
- `env_set`：沿链找到即改，各层都找不到则在当前作用域新建绑定（隐式声明）。

### `ast::RuaError`

```rust
pub struct RuaError { line: usize, msg: String }
```

全项目统一，`main()` 收尾格式化后 `exit(1)`。

## 四、编译流水线细节

### 词法分析（lexer.rs）
- 中文关键字：喵 / 变量 / 如果 / 那么 / 否则 / 当 / 返回 / 喵叫 / 数组；
- 字符串字面量：支持 `""` `''` 与中文全角 `“”` `‘’`，英文引号内支持
  `\n \t \r \\ \" \'` 转义；
- 注释：`#` 单行、`#* ... *#` 多行；
- 标识符字符：英文/数字/下划线/常用汉字（U+4E00~U+9FA5）。

### 语法分析（parser.rs）
- 表达式用「优先级爬升」统一处理二元运算符（`==`/`!=`=1、比较=2、加减=3、
  乘除模=4）；
- 语句按关键字分发：变量/数组/如果/当/返回/表达式语句；
- 顶层 `喵 ...` 单独收集为函数定义（不进入程序语句列表）。

### 运行时（runtime.rs）
- `exec` 按 `Node` 变体递归求值；返回语句通过 `Ctx.returning` 标志向上传递；
- `apply_binop` 按 `TokenKind` 分发：`+` 支持字符串拼接，`==`/`!=` 支持字符串
  内容比较，其余算术/比较要求操作数为数字；
- 函数调用 `exec_call`：`喵叫` 逐参打印（空格分隔、末尾换行）；用户函数新建
  子环境绑形参，先看是否可 VM，是则走 `vm::invoke`。

## 五、字节码 VM（vm.rs）

### 可 VM 判定（对应 C 版 JIT eligibility）
函数全部满足才编译为字节码：
1. 节点类型受限：无 `N_STR`/`N_ARRAY`/`N_INDEX`（`N_STR` 仅允许作 `喵叫` 实参）；
2. 所有 `N_IDENT` 都必须是形参或函数内 `变量` 声明的局部变量（引用全局 → 不可 VM）；
3. 被调用函数也必须可 VM（闭包传播，迭代求解）。

### 栈机指令
`Push / PushStr / LoadVar / StoreVar / Add Sub Mul Div Mod / Eq Ne Gt Lt Ge Le /
Jmp Jz / Call / CallPrint / Ret / Pop`。

### 两遍编译
1. 预扫描判定 + 变量槽位分配 + 闭包传播；
2. 给可 VM 函数按序分配索引（递归/互调编译期可引用），再逐个生成字节码。

### 执行（invoke）
- 帧栈模拟调用；`Call` 压新帧（形参入槽、局部槽补 0），`Ret` 弹帧并把结果
  压回调用方栈顶；
- `CallPrint` 对应内置 `喵叫`（整数与字符串字面量实参）；
- 递归/互调经函数索引表间接调用，运行期天然正确。

### 与 C 版 JIT 的差异
- 用安全栈机字节码替代手写 x86-64 机器码，无 `unsafe`；
- 无寄存器参数上限（放宽到 64）；
- 无显式 `返回` 的函数返回 0（与 C 版 JIT 语义一致；树遍历解释器返回函数体
  最后一条语句的值）。

## 六、CLI

```
rua <源文件.rua> [--debug] [--dump]
```

| 参数 | 行为 |
| --- | --- |
| 无参数 | 打印用法，`exit(1)` |
| `-h` / `--help` | 打印用法，`exit(0)` |
| `--debug` | 输出词法 Token 明细、AST、函数表与可 VM 状态、各阶段耗时 |
| `--dump` | 转储可 VM 函数的字节码（反汇编文本） |

参数顺序无关；`--debug`/`--dump` 输出到 stderr，不污染程序 stdout。

## 七、构建与验证

```bash
cargo build               # debug
cargo build --release     # release
./target/release/rua example/example.rua
```

预期输出对照见 `docs/expected-output.md`（由 C 版修复函数调用子环境 bug 后生成）。

## 八、移植说明（相对 C 版的行为差异）

| 项 | C 版 | Rust 版 |
| --- | --- | --- |
| 运行时算术错误行号 | 部分固定为 0 | 精确源码行号 |
| 可 VM 函数无返回语句 | JIT 返回 0 | 同（VM 返回 0） |
| 数组语义 | 句柄共享 | 同（`Rc<RefCell<Vec<Value>>>`） |
| JIT | x86-64 机器码 | 栈机字节码 VM（纯安全代码） |
| 调试输出 | `-DDEBUG` 编译宏 | `--debug`/`--dump` 命令行参数 |
