# Rua 简易版解释器 — API 文档

(本文档是我用AI完善的 不对的提一下issues pr)

> 适用范围：`easy/` 目录下的 C 语言解释器（头文件式分文件，无构建系统）。
> 本文档把所有函数按**编译流水线**串联起来说明：入口 → 读取文件 → 词法分析 → 语法分析
> → 解释执行（可选 JIT 加速）。
>
> 分两大部分：**第一部分** 解释器（utf8/lexer/ast/parser/runtime）；**第二部分** JIT 编译器（jit*.h）。

---

## 一、总览：整条流水线

```
        main()                     rua.c
          │
          ├──> read_file(path) ──────────────── 读取源码到内存
          │
          ├──> init_bin_ops() ───────────────── 初始化运算符函数指针表
          │
          ├──> compile(src) ─────────────────── 编译
          │        │
          │        ├──> lex_all(src) ────────── 词法分析 → Token 数组
          │        │        └──> lex_next() ── 逐 Token 扫描
          │        │             ├──> lex_string()  字符串字面量
          │        │             ├──> keyword_kind() 关键字识别
          │        │             └──> token_make()   构造 Token
          │        │
          │        ├──> parse_function() ────── 函数定义 → 注册进函数表
          │        │        └──> parse_block() ─ 函数体
          │        │
          │        └──> parse_statement() ───── 顶层语句 → AST
          │                 └──> parse_*() 递归下降
          │
          ├──> [OPTIMIZATION] jit_compile_all(fn_table, fn_count)  ── JIT：纯整数函数 → 机器码
          │        ├──> jit_plan_function()    可 JIT 判定 + vreg 分配 + 帧布局
          │        └──> jit_emit_function()    表达式/语句 → x86-64
          │
          ├──> env_new(NULL) ────────────────── 建立全局环境
          │
          └──> exec(prog, &ctx) ─────────────── 解释执行
                   └──> exec() 递归分发
                        ├──> env_get / env_set / env_bind  变量读写
                        ├──> exec_call()                   函数调用
                        │     ├──> jit_invoke()            JIT 函数（f->jittable）
                        │     └──> find_fn() 查函数表
                        ├──> apply_binop() → bin_table[op] 二元运算
                        │     └──> bin_add() ... bin_le()  各运算符实现
                        ├──> array_get() / array_set()     数组读写
                        └──> print_val()                   喵叫输出
```

错误处理统一走 `error_at()`（打印行号 + 退出）；调试/计时走 `debug.h`（`-DDEBUG` 时生效）。

---

## 二、文件与数据流

| 文件 | 阶段 | 产出的核心数据 |
| --- | --- | --- |
| `includes/utf8.h` | 基础工具 | Unicode 码点 `Char` |
| `includes/lexer.h` | 词法分析 | `Token` 数组 |
| `includes/ast.h` | 语法树 | 通用节点 `Node` |
| `includes/parser.h` | 语法分析 | AST（`Node` 树） |
| `includes/runtime.h` | 解释执行 | `Value`（运行结果） |
| `includes/jit.h` | JIT 入口 | `g_jit_table`（函数地址表） |
| `includes/jit/jit_base.h` | JIT 基础 | 寄存器枚举、平台常量、Jit 状态 |
| `includes/jit/jit_emit.h` | JIT 指令发射 | x86-64 机器码字节 |
| `includes/jit/jit_analyze.h` | JIT 分析 | `FnPlan`（vreg 表/帧布局） |
| `includes/jit/jit_codegen.h` | JIT 代码生成 | 机器码函数 |
| `includes/jit/jit_debug.h` | JIT 反汇编 | 机器码 → Intel 汇编（DEBUG） |
| `includes/debug.h` | 调试/计时 | `[调试]` 输出（`-DDEBUG`） |
| `scripts/` | 构建脚本 | `build_easy_linux.py` / `build_easy_windows.py` |
| `example/` | 示例程序 | `*.rua` 用例 |
| `docs/API.md` | API 文档 | 本文档 |
| `rua.c` | 入口 | 进程退出码 |

核心数据结构之间的流转：

```
源码(char*) ──词法分析──> Token[] ──语法分析──> Node(AST) ──解释执行──> Value
                                                      │
                                      [OPTIMIZATION] └──jit 编译──> x86-64 机器码 ──> int64
```

---

## 三、utf8.h — UTF-8 工具

UTF-8 变长编码的底层工具，词法分析器逐字符扫描时依赖这些函数。

### `Char utf8_peek(const char* p)`
- **作用**：查看指针当前位置的字符（**不解码前进**），返回其 Unicode 码点。
- **参数**：`p` 指向 UTF-8 字符流当前位置。
- **返回**：当前位置字符的码点。
- **被调用于**：`lex_next()`、`lex_string()`。

### `void utf8_advance(const char** p)`
- **作用**：按 UTF-8 规则把指针 `p` 前进一个字符（1~4 字节）。
- **参数**：`p` 指向当前字符起始位置（会被修改）。
- **被调用于**：`lex_next()`、`lex_string()`。

### `int is_ident_char(Char c)`
- **作用**：判断字符能否作为标识符字符（英文/数字/下划线/常用汉字 U+4E00~U+9FA5）。
- **参数**：`c` 待判断的码点。
- **返回**：1 可作标识符，0 不可。
- **被调用于**：`lex_next()`（切分标识符/关键字）。

### `void error_at(const char* msg, int line)` — `noreturn`
- **作用**：统一报错出口。打印「第 N 行：消息」到 stderr 并 `exit(1)`。
- **参数**：`msg` 错误描述；`line` 源码行号（0 表示未知）。
- **被调用于**：几乎所有函数（词法/语法/运行时错误）。

---

## 四、lexer.h — 词法分析

把源码切成 `Token` 流。Token 结构见源码注释（`kind` / `text` / `num` / `line`）。

### `Token token_make(TokenKind kind, long long num, int line)`
- **作用**：构造一个基础 Token（`text` 置 NULL）。
- **返回**：填好 `kind/num/line` 的 Token。
- **被调用于**：`lex_next()`、`lex_string()`。

### `TokenKind keyword_kind(const char* s)`
- **作用**：把切出的文本与关键字表比对（喵/变量/如果/那么/否则/当/返回/喵叫/数组）。
- **返回**：命中返回关键字类型，否则 `TK_IDENT`。
- **被调用于**：`lex_next()`。

### `Token lex_string(Lexer* lx, Char q, int line)`
- **作用**：扫描字符串字面量，支持英文引号 `"` `'` 与中文全角引号 `“”` `‘’`，处理 `\n \t \r \\ \" \'` 转义。
- **参数**：`q` 起始引号（决定结束引号）；内容存入 `Token.text`（malloc）。
- **被调用于**：`lex_next()`。

### `Token lex_next(Lexer* lx)`
- **作用**：扫描下一个 Token。跳过空白与注释（`#` 单行、`#* ... *#` 多行），依次识别数字、标识符/关键字、字符串、运算符。
- **返回**：下一个 Token；扫完返回 `TK_EOF`。
- **被调用于**：`lex_all()`。

### `Token* lex_all(const char* src, int* count)`
- **作用**：对整段源码执行词法分析，收集全部 Token。
- **参数**：`src` 源码；`count` 输出 Token 个数（含 EOF）。
- **返回**：malloc 的 Token 数组（调用方负责释放）。
- **被调用于**：`compile()`。

---

## 五、ast.h — 语法树

### `Node* new_node(NodeKind kind, int line)`
- **作用**：`calloc` 清零创建 AST 节点，只填类型与行号。
- **返回**：新节点指针。
- **被调用于**：所有 `parse_*()` 函数。

> `struct Node` 是通用结构，字段按 `kind` 复用，字段含义见 `ast.h` 内注释。

---

## 六、parser.h — 语法分析

递归下降解析器，每个文法规则对应一个 `parse_*`。表达式按优先级分层。

### 基础读取（无出错时取 Token）
- **`TokenKind peek(Parser* ps)`**：查看当前 Token 类型（不前进）。被 `expect`/`parse_*` 调用。
- **`Token* advance(Parser* ps)`**：取走当前 Token 并前进。被 `expect`/`parse_*` 调用。
- **`TokenKind expect(Parser* ps, TokenKind k)`**：期望当前 Token 为 `k`，不符则报错；否则取走。被各 `parse_*` 调用。
- **`char* expect_ident(Parser* ps)`**：期望标识符并返回其名字（malloc 副本）。被 `parse_var`/`parse_array`/`parse_function` 调用。

### 表达式（优先级爬升，统一处理）
| 函数 | 作用 |
| --- | --- |
| `op_prec(k)` | 返回二元运算符优先级（`==`/`!=`=1、比较=2、加减=3、乘除模=4），非运算符为 0 |
| `parse_expr_prec(ps, min_prec)` | **优先级爬升**：解析原子后循环，取走优先级 ≥ `min_prec` 的运算符并递归右操作数（右操作数 +1 保证左结合） |
| `parse_primary(ps)` | 解析原子：数字/字符串/括号/标识符/喵叫，再处理后缀（调用、下标） |
| `parse_expr(ps)` | 先做二元运算爬升，紧跟 `=` 则包装成 `N_ASSIGN` |

> 用「优先级爬升」替代了原先 4 个几乎相同的 parse_equality / parse_comparison / parse_addition / parse_multiplication 函数。

### 辅助（动态数组扩容）
- **`node_list_add(list, n, cap, item)`**：往节点数组尾部追加（满则 ×2 扩容）。被 `parse_call_args`/`parse_block`/`compile` 调用。
- **`str_list_add(list, n, cap, item)`**：往字符串数组尾部追加。被 `parse_function` 调用。
- **`parse_call_args(ps, args, nargs)`**：解析调用实参 `( 表达式, ... )`。被 `parse_primary` 的喵叫分支与普通调用分支共用。

### 语句与函数
| 函数 | 语法 |
| --- | --- |
| `parse_var(ps)` | `变量 名 = 表达式` → `N_VAR` |
| `parse_array(ps)` | `数组 名[长度] = { 值 }` → `N_ARRAY` |
| `parse_if(ps)` | `如果 条件 那么? { } 否则? { }` → `N_IF` |
| `parse_while(ps)` | `当 条件 那么? { }` → `N_WHILE` |
| `parse_return(ps)` | `返回 表达式?` → `N_RETURN` |
| `parse_statement(ps)` | 按关键字分发语句；其余按表达式语句处理 |
| `parse_block(ps)` | `{ 语句* }` → `N_BLOCK` |
| `parse_function(ps)` | `喵 名(形参*) { 体 }` → `N_FUNC` |

调用关系：`parse_block` ↔ `parse_statement` 互相递归；`parse_function` → `parse_block`。

---

## 七、runtime.h — 解释执行

### 值与环境
- **`Value num_val(long long n)`**：构造数字值。被 `exec` 系列与各 `bin_*` 调用。
- **`Env* env_new(Env* parent)`**：新建环境（优先从空闲链表取，O(1)）。被 `exec_call`、`main` 调用。
- **`void env_release(Env* e)`**：把环境及其绑定数组回收进空闲链表。被 `exec_call` 调用。
- **`void env_bind(Env* e, const char* name, Value v)`**：在当前作用域绑定新变量（自动扩容）。被 `exec`（`N_VAR`/`N_ARRAY`）与 `env_set`/`exec_call` 调用。
- **`Value env_get(Env* e, const char* name)`**：沿作用域链查找变量值，找不到报错。被 `exec`（`N_IDENT`）调用。
- **`void env_set(Env* e, const char* name, Value v)`**：沿链找到变量并改写，找不到则在当前作用域新建。被 `assign_target` 调用。

### 函数表
- **`Fn* find_fn(const char* name)`**：按名查函数表，返回记录或 NULL。被 `exec_call` 调用。
- **`void register_fn(Node* f)`**：把 `N_FUNC` 节点注册进函数表（复制名字/形参，共享函数体）。被 `compile()` 调用。
- **`Fn` 字段**：`jittable`（1 = 已 JIT）、`jit_addr`（机器码入口），由 `jit_compile_all` 写入。

### 表达式与语句求值
- **`int is_true(Value v)`**：真值判断（数字≠0、字符串非空、数组恒真）。被 `exec` 的 `N_IF`/`N_WHILE` 调用。
- **`Value array_get(Value base, Value idx, int line)`**：`arr[idx]` 读取，越界报错。被 `index_read` 调用。
- **`void array_set(Value base, Value idx, Value v, int line)`**：`arr[idx]=v` 写入，越界报错。被 `assign_target` 调用。
- **`Value index_read(Node* n, Ctx* ctx)`**：求值 `N_INDEX`（先求基表达式和下标）。被 `exec` 调用。
- **`void assign_target(Node* target, Value v, Ctx* ctx)`**：把值写入赋值目标（变量或数组元素）。被 `exec`（`N_ASSIGN`）调用。
- **`Value print_val(Value v)`**：打印一个值（数字/字符串/数组），不换行。被 `exec_call`（喵叫）调用。
- **`Value exec_call(Node* n, Ctx* ctx)`**：执行函数调用。`喵叫` 逐参数打印；用户函数先看 `f->jittable`——是则走 `jit_invoke` 调机器码，否则新建子环境绑形参、求值函数体后回收环境。被 `exec` 调用。
- **`Value exec(Node* n, Ctx* ctx)`**：**解释执行主分发**。按节点 `kind` 递归求值：
  - 字面量/标识符 → 取值；
  - `N_VAR`/`N_ARRAY`/`N_ASSIGN`/`N_INDEX` → 声明/赋值/读写；
  - `N_BINARY` → `apply_binop`；
  - `N_CALL` → `exec_call`；
  - `N_IF`/`N_WHILE` → `is_true` 分支/循环；
  - `N_RETURN` → 置 `ctx->returning` 向上传递返回值；
  - `N_PROG`/`N_BLOCK` → 逐语句执行，遇返回提前结束。
  - 被 `main` 调用（程序入口）。

### 内置二元运算符（FP 风格：函数指针表分发）
- **`typedef Value (*BinOp)(Value, Value)`**：运算符函数指针类型。
- **`char* num_to_str(long long n)`**：数字转字符串。被 `bin_add` 调用。
- 各运算符实现（要求操作数均为数字，除 `bin_add` 支持字符串拼接、`bin_eq`/`bin_ne` 支持字符串比较）：

| 函数 | 运算符 | 说明 |
| --- | --- | --- |
| `bin_add(a, b)` | `+` | 数字相加；任一侧为字符串则拼接 |
| `bin_sub(a, b)` | `-` | 相减 |
| `bin_mul(a, b)` | `*` | 相乘 |
| `bin_div(a, b)` | `/` | 相除，除 0 报错 |
| `bin_mod(a, b)` | `%` | 取模，模 0 报错 |
| `bin_eq(a, b)` | `==` | 相等（经 `values_equal`） |
| `bin_ne(a, b)` | `!=` | 不等 |
| `bin_gt(a, b)` | `>` | 大于 |
| `bin_lt(a, b)` | `<` | 小于 |
| `bin_ge(a, b)` | `>=` | 大于等于 |
| `bin_le(a, b)` | `<=` | 小于等于 |

> `bin_sub`/`bin_mul` 由宏 `DEFINE_ARITH_BINOP` 生成；`bin_gt`/`bin_lt`/`bin_ge`/`bin_le` 由宏 `DEFINE_CMP_BINOP` 生成，避免重复代码。

- **`int values_equal(Value a, Value b)`**：相等判断（字符串比内容，数字比值）。被 `bin_eq`/`bin_ne` 调用。
- **`void init_bin_ops(void)`**：把运算符 TokenKind 映射到实现函数，填 `bin_table`。被 `main` 调用一次。
- **`Value apply_binop(int op, Value a, Value b)`**：查 `bin_table[op]` 函数指针并调用。被 `exec`（`N_BINARY`）调用。

---

## 八、rua.c — 入口

### `Node* compile(const char* src)`
- **作用**：编译整段源码。词法分析出 Token 后，顶层循环：`喵` 开头 → `parse_function` 并 `register_fn` 注册；其余 → `parse_statement` 收入程序节点。
- **返回**：`N_PROG` 程序节点。调用 `lex_all`、`parse_function`、`parse_statement`、`register_fn`。
- **被调用于**：`main()`。

### `char* read_file(const char* path)`
- **作用**：读取整个文件到 malloc 缓冲区并以 `\0` 结尾；打开失败报错退出。
- **被调用于**：`main()`。

### `int main(int argc, char* argv[])`
- **作用**：程序入口。用法 `rua <源文件.rua>`。
  1. 无参或 `-h/--help` → 打印用法；
  2. `init_bin_ops()` 初始化运算符表；
  3. `read_file` → `compile`；
  4. `-DOPTIMIZATION` 时调用 `jit_compile_all(fn_table, fn_count)` 预编译可 JIT 函数；
  5. `env_new(NULL)` 建全局环境 → `exec(prog, &ctx)` 执行。

---

## 九、调用关系速查（谁调用谁）

**编译期（rua.c → parser.h/lexer.h/ast.h）**
```
compile
 ├─ lex_all → lex_next → lex_string / keyword_kind / token_make / utf8_*
 ├─ parse_function → parse_block ↔ parse_statement → parse_var/parse_array/parse_if/parse_while/parse_return/parse_expr
 │        └─ parse_expr → parse_expr_prec → parse_primary → parse_call_args
 └─ register_fn
```

**运行期（rua.c → runtime.h）**
```
main → exec(prog, &ctx)
        └─ exec 递归分发
            ├─ env_bind / env_get / env_set / env_new / env_release
            ├─ exec_call → (f->jittable ? jit_invoke : 解释执行) / find_fn
            ├─ apply_binop → bin_table[op] → bin_add ... bin_le
            ├─ array_get / array_set / index_read / assign_target
            ├─ is_true
            └─ print_val
```

**JIT 期（OPTIMIZATION，rua.c → jit.h）**
```
jit_compile_all
 ├─ jit_plan_function → jit_count_stmt / jit_build_vars / jit_scan_side_effects / jit_scan_callees
 └─ jit_emit_function → jit_compile_stmt → jit_compile_expr → jit_emit_binop / jit_emit_call
        └─ jit_emit_*（指令发射器）/ jit_new_label / jit_bind_label / jit_load_reg / jit_store_reg / ...
```

**所有报错** → `error_at()`（`exit(1)`）。

---

# 第二部分：JIT 编译器（easy/jit*.h）

> JIT 把**纯整数用户函数**直接从 AST 编译成 x86-64 机器码，其余代码继续由解释器执行。
> 同时支持 Linux（SysV）与 Windows（MSVC x64）。

## 十、JIT 目标与范围

| 项目 | 决策 |
| --- | --- |
| 编译源 | **AST 直接编译**成 x86-64 机器码（不引入字节码层） |
| 作用域 | 只 JIT **用户函数**（`喵` 定义的）；顶层语句继续解释执行 |
| 寄存器策略 | **固定 vreg→物理寄存器映射 + v14+ 溢出到栈** |
| 值模型 | JIT 函数内**只有 int64**，无字符串/数组/标签联合 |
| 平台 | Linux x86-64（SysV）与 Windows x64（MSVC） |
| 启用 | 编译期宏 `-DOPTIMIZATION`（构建脚本默认开启） |
| 代码风格 | 函数化：全部 `static` 自由函数；**引用一律用指针** |

设计原则（贯穿全部 JIT API）：
- 状态用一个 `Jit` 结构体承载，所有函数第一个参数都是 `Jit* jit`；
- AST 节点一律以 `const Node*` 传入，**不传结构体值**；
- 输出类结果用**出参指针**（`int*` 等），避免返回大结构体；
- 运行期数据（`Fn*`、`Jit*`）一律指针引用，不用拷贝。

## 十一、总体流程：与解释器的衔接

```
compile(src)  ──> 解析出 AST，注册 Fn 到 fn_table（同解释器）

    ┌─ 若定义 -DOPTIMIZATION ──────────────────────────────┐
    │   jit_compile_all(fn_table, fn_count)                │
    │     1) 逐函数判定"可 JIT"（见 §12）                   │
    │     2) 两遍编译：先占表，再发射机器码（见 §17）       │
    │     3) 给每个 Fn 写入 jittable / jit_addr             │
    └───────────────────────────────────────────────────────┘

exec(prog, &ctx) ──> exec_call()
    │   n->str == "喵叫"  →  解释器逐参打印（不变）
    │   f->jittable        →  jit_invoke() 直接调机器码
    │   否则                →  走原 AST 递归求值（解释路径）
```

`JitFn` 类型与调用约定：

```c
typedef int64_t (*JitFn)(int64_t, int64_t, int64_t, int64_t, int64_t, int64_t);
```

> JIT 函数按**平台原生整数调用约定**生成，C 侧把它当普通函数指针直接调用即可，
> 无需手写汇编蹦床。若调用方传了非数字实参，JIT 侧读到的是该 `Value` 的 `num` 字段
> （字符串/数组的 `num` 为 0），与原版行为一致。

## 十二、可 JIT 判定（eligibility）

对每个函数做一次 AST 预扫描，全部满足才算可 JIT：

1. **参数个数**：Linux `≤ 6`；Windows `≤ 4`（寄存器参数上限）；
2. **引用受限**：函数体内出现的所有 `N_IDENT` 都必须是**形参或函数内 `变量` 声明的局部变量**（引用全局变量 → 不可 JIT）；
3. **节点类型受限**：只允许 `N_NUM`、`N_IDENT`、`N_BINARY`、`N_ASSIGN`、`N_IF`、`N_WHILE`、`N_RETURN`、`N_CALL`（`N_BINARY` 含 `/` `%`，由 `idiv` 实现）；
4. **打印（`喵叫`）**：仅当每个实参都是**整数表达式或字符串字面量**（`N_STR`）时才可 JIT；含字符串变量/拼接、数组实参 → 不可 JIT；
5. **闭包传递**：被调用的函数不可 JIT → 调用者也不可 JIT（自底向上迭代，同原项目 `canJIT`）。

判定结果写入 `f->jittable`；`exec_call` 只信这个标记。

## 十三、虚拟寄存器分配（AST → vreg）

我们没有字节码，所以在预扫描时给 AST 分配 vreg：

```
v0        = 结果/返回值寄存器（始终由 RAX 承载）
v1..vP    = 形参（P = nparams）
vP+1..    = 每个 "变量 名 = ..." 声明各占一个 vreg（同名字同 vreg，后续赋值复用）
...        = 每个 N_BINARY 节点各占一个 vreg（临时值；节点之间不冲突，永不复用）
```

分配规则（`jit_build_vars` + `jit_count_stmt` 一次遍历完成，与代码生成**同序推进计数器**）：
- 形参按声明顺序 v1, v2, …；
- `N_VAR` 登记 名字→vreg；
- 每个 `N_BINARY` 节点分配两个操作数临时 vreg；
- 记录**用到的最大 vreg**（`max_vreg`），据此算出溢出槽数量与需要保存的 callee-saved 寄存器。

标识符解析：预扫描建 名字→vreg 表；若 `N_IDENT` 查不到（全局/未声明）→ 该函数不可 JIT。

## 十四、物理寄存器映射（vreg → preg，按平台）

### Linux（SysV）

```
v0  → RAX     (caller-saved)  结果/返回值
v1  → RBX     (callee-saved)  形参1
v2  → R12     (callee-saved)  形参2
v3  → R13     (callee-saved)  形参3
v4  → R14     (callee-saved)  形参4
v5  → R15     (callee-saved)  形参5
v6  → RSI     (caller-saved)  形参6
v7  → RDI     (caller-saved)
v8  → RDX     (caller-saved)
v9  → RCX     (caller-saved)
v10 → R8      (caller-saved)
v11 → R9      (caller-saved)
v12 → R10     (caller-saved)
v13 → R11     (caller-saved)
v14+ → 栈溢出槽（[rbp - k*8]）
```

入参拷贝（序言中）：`v1←RDI, v2←RSI, v3←RDX, v4←RCX, v5←R8, v6←R9`。

### Windows（MSVC x64）

Windows 只有 4 个寄存器参数，且 RCX/RDX/R8/R9 是 caller-saved，因此把形参放到 callee-saved 寄存器：

```
v0  → RAX     (caller-saved)  结果/返回值
v1  → RBX     (callee-saved)  形参1
v2  → RDI     (callee-saved)  形参2
v3  → RSI     (callee-saved)  形参3
v4  → R12     (callee-saved)  形参4
v5  → R13     (callee-saved)
v6  → R14     (callee-saved)
v7  → R15     (callee-saved)
v8  → RCX     (caller-saved)
v9  → RDX     (caller-saved)
v10 → R8      (caller-saved)
v11 → R9      (caller-saved)
v12 → R10     (caller-saved)
v13 → R11     (caller-saved)
v14+ → 栈溢出槽
```

入参拷贝：`v1←RCX, v2←RDX, v3←R8, v4←R9`（Windows 上限 4 个，见 §12）。

> 映射表是编译期常量表 `JIT_VREG_TO_PHYS[14]`，代码生成器通过它把 vreg 翻译成
> 物理寄存器号，**不写死分支**。

## 十五、函数序言 / 尾声与栈布局

```
入栈方向 ↓

  [rbp+8]  返回地址
  [rbp]    保存的旧 rbp（caller 的 rbp）
  …        保存的 callee-saved 寄存器（用到的才存）
  …        溢出槽 v14+（每个 8 字节）
  …        caller-saved 快照槽（仅含调用/打印/除模时，见 §17/§16.4）
  …        Windows shadow space(32B) / 对齐填充
```

序言（`jit_emit_function`）：
1. `push rbp`；`mov rbp, rsp`；
2. 按分析结果 `push` 用到的 callee-saved 寄存器；
3. `sub rsp, frame_bytes`（溢出槽 + 快照槽 + shadow + 对齐填充）；
4. 清 RAX（无显式返回时返回值 0）；
5. 把入参从 ABI 参数寄存器拷到各自 vreg。

对齐（SysV/Windows 通用）：`call` 前 RSP 必须 16 字节对齐。入口 RSP ≡ 8 (mod 16)（`call` 压了返回地址），
按「pushed_bytes + frame_bytes ≡ 8 (mod 16)」反推帧大小；Windows 额外把 32 字节 shadow space 并入帧。

尾声：`add rsp, frame_bytes` → 逆序 `pop` callee-saved → `pop rbp` → `ret`。
返回值为 v0（RAX），RET 前不额外搬移。

## 十六、指令发射与代码生成规则

### 16.1 发射器（jit_emit.h，写字节到代码缓冲）

| 函数 | 作用 |
| --- | --- |
| `jit_emit8 / jit_emit32 / jit_emit64` | 写 1/4/8 字节 |
| `jit_emit_rex` / `jit_emit_modrm` | REX 前缀 / ModRM 字节 |
| `jit_emit_mov` / `jit_emit_mov32` / `jit_emit_mov64` | MOV 系列（imm32 用 C7 /0，imm64 用 movabs） |
| `jit_emit_load_rbp` / `jit_emit_store_rbp` | 帧内读写 [rbp+disp32] |
| `jit_emit_load_rax_idx` | mov rax, [rax+disp32]（读函数表项） |
| `jit_emit_add / sub / imul / cmp / test`（含 *_mem 变体） | 算术/比较 |
| `jit_emit_setcc_al` + `jit_emit_movzx_al` | 比较结果 → 0/1 |
| `jit_emit_cqo` / `jit_emit_idiv_reg` / `jit_emit_idiv_mem` | 符号扩展 / 除法 |
| `jit_emit_push / pop`、`jit_emit_sub_rsp / add_rsp` | 栈操作 |
| `jit_emit_call_reg`、`jit_emit_ret`、`jit_emit_xor_eax` | 调用/返回/清零 |

标签（`jit_emit.h`）：`jit_new_label` / `jit_bind_label` / `jit_emit_jmp` / `jit_emit_jcc`。
后向跳转直接算偏移；前向跳转先发 `rel32` 占位、记入回填表，`jit_bind_label` 时统一回填。

### 16.2 表达式 → 机器码（jit_codegen.h `jit_compile_expr`）

统一"把表达式求值结果放进 `dst` vreg"：

| AST 节点 | 发射内容 |
| --- | --- |
| `N_NUM` | `mov dst, imm` |
| `N_IDENT` | `mov dst, src`（src = 该变量的 vreg，查变量表） |
| `N_BINARY(op)` | 左值 → 左 vreg → 右值 → 右 vreg；按 op 发射 ADD/SUB/IMUL/CMP+SETcc… 结果写 dst |
| `N_CALL` | 见 §17 |

`/` `%`（`idiv` 序列）：被除数搬到 RAX → `cqo` → `idiv 除数`（商→RAX，余数→RDX）→ 结果写 dst。
`idiv` 会破坏 RAX/RDX，因此先快照 caller-saved（RDX 在其中），算完恢复。

### 16.3 语句 → 机器码（`jit_compile_stmt`）

| AST 节点 | 发射内容 |
| --- | --- |
| `N_VAR` / `N_ASSIGN` | 初始化表达式直接求值进目标变量 vreg |
| `N_IF` | 条件 → vreg → `test; je else_label`；then 块；`jmp done`；else 块；`done:` |
| `N_WHILE` | `loop:` 条件 → vreg → `test; je done`；body；`jmp loop`；`done:` |
| `N_RETURN` | 求值 → RAX；`jmp epilogue` |
| 其它表达式语句 | 调用后丢弃结果 |

### 16.4 JIT 里的 `喵叫`（打印）

辅助函数（C 实现，JIT 代码调用，`jit_base.h`）：

```c
static const char JIT_ENDL_NL[] = "\n";
static const char JIT_ENDL_SP[] = " ";
static char jit_scratch[64];
static void  jit_print(const char* msg, const char* endl);  /* 打印 msg 后接 endl */
static char* jit_itoa(int64_t val, char* buf);              /* 整数→十进制 */
```

编译规则（`jit_emit_call`，callee == "喵叫"）：
- **先快照 caller-saved**（含实参临时值），实参从快照槽读取，避免被先前的打印调用破坏；
- 逐实参打印一次：**非末参**用 `JIT_ENDL_SP`（空格），**末参**用 `JIT_ENDL_NL`（换行），与解释器逐字节一致；
- **字符串字面量实参**：字面量拷进 JIT 数据缓冲，`mov reg, imm64(数据区地址)` 取指针；
- **整数实参**：先 `jit_itoa` 格式化成全局 `jit_scratch`，再传该缓冲指针；
- 调用前后经快照槽保存/恢复 caller-saved。

`Jit` 状态含两块缓冲：**代码缓冲**（可执行，RWX）与**数据缓冲**（字符串字面量），
地址在发射时已知（imm64 直接嵌入机器码）。

## 十七、函数调用与递归（两遍编译 + 函数表）

```c
static int64_t g_jit_table[256];   /* 按函数下标存机器码入口地址 */
```

- **第一遍**：给所有可 JIT 函数在表中占位（非零哨兵），使递归/互调在编译期就能引用；
- **第二遍**：逐个把函数编译进代码缓冲，把入口地址写回表 + 写回 `f->jit_addr`。

JIT 函数内调用另一个 JIT 函数（`jit_emit_call`）：
1. 快照 caller-saved 到帧内槽位；
2. 把实参 vreg 装入 ABI 参数寄存器（caller-saved 源从快照槽读，避免装载顺序互相覆盖）；
3. 间接调用：`rax = g_jit_table 基址；rax = [rax + funcIdx*8]；call rax`；
4. 恢复 caller-saved；
5. `mov dst, rax`（结果 → 目标 vreg）。

> 通过 `g_jit_table` 间接调用，递归在"地址尚未回填"时也能正确工作——
> 因为调用发生在运行期，那时表早已填好。

## 十八、平台抽象（jit_base.h 内，`#ifdef`）

| 功能 | Linux | Windows |
| --- | --- | --- |
| 分配可执行内存 | `mmap(NULL, size, PROT_READ\|WRITE\|EXEC, MAP_PRIVATE\|ANONYMOUS, -1, 0)` | `VirtualAlloc(NULL, size, MEM_RESERVE\|MEM_COMMIT, PAGE_EXECUTE_READWRITE)` |
| 收尾权限 | 保持 RWX（简单） | 同左 |
| ABI 差异 | 集中在常量表（§14）与序言（§15） | 同左 |

宏开关：`-DPLATFORM_LINUX` / `-DPLATFORM_WINDOWS`，代码内 `_WIN32` / `_MSC_VER` 兜底。

## 十九、启用方式与调试

```bash
# Linux（easy/scripts/build_easy_linux.py，默认 JIT + 调试）
python3 easy/scripts/build_easy_linux.py                # -O2 -DOPTIMIZATION -DDEBUG
python3 easy/scripts/build_easy_linux.py --no-debug     # 关调试输出
python3 easy/scripts/build_easy_linux.py --no-jit       # 纯解释

# Windows（easy/scripts/build_easy_windows.py，需 VS 开发者命令行，默认 JIT + 调试）
python3 easy/scripts/build_easy_windows.py              # /O2 /DOPTIMIZATION /DDEBUG
```

- `-DOPTIMIZATION`：启用 JIT（不定义则纯解释）；
- `-DDEBUG` 详细输出（走 `debug.h` 的 `DBG_PRINT` / `DBG_DUMP_HEX`）：
  - **编译期**：逐 Token 明细（行号/类型/文本/数值）、顶层语句与函数清单（形参）、
    JIT 可判定结果（不可 JIT 时附**具体原因**：参数超限 / 含不支持的节点或字符串值 /
    引用全局变量 / 调用了不可 JIT 的函数）；
  - **JIT 编译**：每个函数的最大 vreg、参数与局部计数、溢出槽数、帧大小、
    需保存的 callee-saved 寄存器、变量→vreg 表、被调用函数表、完整机器码 hexdump；
  - **运行期**：每次"解释器 → JIT"边界调用（函数名、实参、返回值）与各阶段耗时。
- 验证用 `example/bench_fib.rua`：JIT 后应比解释模式快一个数量级。

## 二十、模块结构与 API（函数化 + 指针）

`rua.c` 里 `-DOPTIMIZATION` 时只包含 `jit.h`（汇总头）并在 `compile()` 之后调用 `jit_compile_all`。

```
easy/includes/jit_base.h     基础：寄存器枚举、平台常量（vreg→物理映射/ABI 参数寄存器）、
                        打印辅助（jit_print/jit_itoa）、可执行内存、Jit 状态、字面量数据区
easy/includes/jit_emit.h     指令发射器（jit_emit_*）与标签回填
easy/includes/jit_analyze.h  vreg 工具（jit_load_reg/store_reg/...）、变量表、可 JIT 判定、帧布局
easy/includes/jit_codegen.h  表达式/语句 → 机器码（jit_compile_expr/stmt、jit_emit_function）
easy/includes/jit_debug.h    机器码反汇编输出（仅 DEBUG）
easy/includes/jit.h          入口：两遍编译 jit_compile_all、解释器分发 jit_invoke
```

> 实际目录结构：`jit_base/emit/analyze/codegen/debug.h` 放在 `easy/includes/jit/` 子目录，
> `jit.h` 汇总头在 `easy/includes/`，内部以 `#include "jit/jit_xxx.h"` 引入。
> 依赖单向：`jit_base → jit_emit → jit_analyze → jit_codegen → jit.h`，无循环。

```c
/* jit.h 对外 API（全部 static 自由函数，全部指针传参） */

typedef int64_t (*JitFn)(int64_t, int64_t, int64_t,
                         int64_t, int64_t, int64_t);  /* 机器码函数签名 */

/* 打印辅助（§16.4，JIT 代码调用） */
static const char JIT_ENDL_NL[];
static const char JIT_ENDL_SP[];
static char jit_scratch[64];
static void  jit_print(const char* msg, const char* endl);
static char* jit_itoa(int64_t val, char* buf);

/* 编译入口：两遍编译，写回 table[i]->jittable / ->jit_addr */
static void jit_compile_all(Fn** table, int count);

/* 解释器 → JIT 分发（exec_call 调用） */
static int64_t jit_invoke(const Fn* f, const Value* args, int nargs);

/* 发射器与内部函数（jit_emit8/32/64、jit_emit_mov/jit_emit_add/... 等） */
```

> 模块内所有函数同样 `static` 且一律传指针，
> 发射器都带 `Jit* jit` 第一个参数，AST 一律 `const Node*`。

## 二十一、构建与测试

1. 改动文件：`jit*.h`（JIT 本体）；`runtime.h` 给 `Fn` 加 `jittable`/`jit_addr` 并在
   `exec_call` 分发；`rua.c` 在 `compile()` 后按 `-DOPTIMIZATION` 调用 `jit_compile_all`。
2. 编译验证（§19），`-Wall -Wextra` 双模式零警告。
3. 正确性回归：`easy/example/*.rua`、`example/*.rua` 输出与纯解释模式逐字节一致。
4. 性能：`bench_fib.rua` 对比 `-DOPTIMIZATION` 前后耗时（应快一个数量级）。
5. Windows：在 MSVC 环境编译运行同一批用例。

## 二十二、实现状态与差异（相对最初设计稿）

**已实现**（Linux 验证通过）：
- AST → x86-64 直编、固定 vreg 映射 + v14+ 溢出、两遍编译、递归/互调；
- 函数内 `喵叫`（整数与字符串字面量实参，`jit_print`/`jit_itoa`）；
- `/` `%`（`cqo + idiv`）；`如果`/`当`/`返回`/赋值/局部变量；
- 可 JIT 判定 + 闭包传递；解释器按 `f->jittable` 分发。

**实现差异**：
| 设计稿 | 实现 |
| --- | --- |
| 独立 `jit_new/jit_destroy` 等 API | 简化为 `jit_compile_all(Fn** table, int count)` 内部管理 `Jit`；`jit_invoke` 为唯一运行时入口 |
| itoa 放帧内 32B 槽 | 改用全局 `jit_scratch[64]`（单线程、打印即时使用，语义等价且省 lea） |
| `mov r64, imm32` 用 B8+r | 必须用 `C7 /0`（B8+r 配 REX.W 是 movabs imm64，会吞掉后续字节）——实现时踩过的坑 |
| 帧对齐条件 `(pushed+frame)%16==0` | 应为 **`==8`**（SysV 入口 RSP≡8），内部 CALL 才对齐 |
| 打印实参读取 | 先在循环前快照 caller-saved，实参从快照槽读取（`jit_load_arg_reg`），避免先后打印调用破坏实参临时值 |

**性能（本机 Linux x86-64，`bench_fib.rua`）**：JIT 约 **0.26s**，纯解释约 **6.7s**，约 **26×** 加速，输出逐字节一致。

## 附：与 C++ 原版 JIT 的对应关系

| C++ 原版 | 本实现 |
| --- | --- |
| 字节码 → `JITCompiler::compileFunction` | AST → `jit_compile_all` |
| vreg 来自字节码寄存器 | §13 预扫描分配 vreg |
| `canJIT`（无 MOVS/DIV/MOD/数组） | §12（无字符串/数组/全局引用 + 闭包传递） |
| `g_jitTable` + 两遍编译 | §17 同样两遍 |
| `arch/JITPlatform`（mmap/VirtualAlloc） | §18 内联 `#ifdef` 抽象 |
| `jit_print_int`（PRINT 可 JIT，仅整数） | 扩展为 `jit_print(msg, endl)` + `jit_itoa`：整数与字符串字面量实参均可 JIT |
