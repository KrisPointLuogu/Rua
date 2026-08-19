// src/ast.rs — 语法树与共享错误类型
//
// 对应 C 版 includes/ast.h（共享错误类型对应 includes/utf8.h 的 error_at）。
// 定义抽象语法树（AST）节点枚举：由语法分析器（parser.rs）生成，
// 交给运行时（runtime.rs）与字节码 VM（vm.rs）消费。
//
// C 版用「通用 struct Node + kind 字段复用」承载所有节点；Rust 版改用
// 枚举变体精确按类型承载字段，每个变体都自带源码行号（报错定位用）。
//
// 共享错误类型 RuaError 全项目统一使用（对应 C 版 error_at：
// 「第 N 行：消息」+ 退出码 1），由 main.rs 收尾统一格式化后 exit(1)。

use crate::lexer::TokenKind;

/**
 * AST 节点类型（对应 C 版 NodeKind）。
 *
 * N_ 前缀说明（C 版命名）：
 *   - 表达式：N_NUM（数字）、N_STR（字符串）、N_IDENT（标识符/变量读取）、
 *     N_BINARY（二元运算）、N_CALL（函数调用）、N_INDEX（数组下标读取）；
 *   - 语句：N_VAR（变量声明）、N_ARRAY（数组声明）、N_ASSIGN（赋值）、
 *     N_IF（如果）、N_WHILE（当）、N_RETURN（返回）、N_BLOCK（代码块）；
 *   - 顶层：N_PROG（程序）、N_FUNC（函数定义）。
 */
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum NodeKind {
    /// 数字（N_NUM）。
    Num,
    /// 字符串（N_STR）。
    Str,
    /// 变量引用（N_IDENT）。
    Ident,
    /// 变量声明（N_VAR）。
    Var,
    /// 数组声明（N_ARRAY）。
    Array,
    /// 赋值（N_ASSIGN）。
    Assign,
    /// 数组下标（N_INDEX）。
    Index,
    /// 二元运算（N_BINARY）。
    Binary,
    /// 函数调用（N_CALL）。
    Call,
    /// 如果（N_IF）。
    If,
    /// 当循环（N_WHILE）。
    While,
    /// 返回（N_RETURN）。
    Return,
    /// 代码块（N_BLOCK）。
    Block,
    /// 程序（N_PROG）。
    Prog,
    /// 函数定义（N_FUNC）。
    Func,
}

/**
 * AST 节点（对应 C 版通用 struct Node，用 Rust 枚举精确按类型承载字段）。
 *
 * 各变体的字段含义：
 *   - Num：      num 保存整数值；
 *   - Str：      text 保存字符串内容；
 *   - Ident：    name 保存变量名；
 *   - Var：      name 变量名，right 初始化表达式；
 *   - Array：    name 数组名，len 长度，right 广播初始值表达式；
 *   - Assign：   target 赋值目标（Ident 或 Index），right 新值；
 *   - Index：    base 被索引的基表达式，index 下标表达式；
 *   - Binary：   op 运算符，left/right 两个操作数；
 *   - Call：     name 函数名，args 实参列表；
 *   - If：       cond 条件，then 真分支块，else_b 假分支块（可空）；
 *   - While：    cond 条件，body 循环体块；
 *   - Return：   value 返回值表达式（可为空）；
 *   - Block：    stmts 语句列表；
 *   - Prog：     顶层语句列表（同 Block 用法）；
 *   - Func：     name 函数名，params 形参表，body 函数体块。
 *
 * 所有带 line 的变体，line 为对应源码行号（用于报错定位）。
 */
#[derive(Debug, Clone)]
pub enum Node {
    /// 数字字面量：num 为整数值。
    Num { line: usize, num: i64 },
    /// 字符串字面量：text 为内容。
    Str { line: usize, text: String },
    /// 标识符（变量读取）：name 为变量名。
    Ident { line: usize, name: String },
    /// 变量声明：`变量 名 = 表达式`，right 为初始化表达式。
    Var {
        line: usize,
        name: String,
        right: Box<Node>,
    },
    /// 数组声明：`数组 名[长度] = { 广播值 }`，right 为广播初始值表达式。
    Array {
        line: usize,
        name: String,
        len: i64,
        right: Box<Node>,
    },
    /// 赋值：target 为赋值目标（变量 Ident 或数组下标 Index），right 为新值。
    Assign {
        line: usize,
        target: Box<Node>,
        right: Box<Node>,
    },
    /// 数组下标读取：base[index]。
    Index {
        line: usize,
        base: Box<Node>,
        index: Box<Node>,
    },
    /// 二元运算：op 是运算符对应的 TokenKind，left/right 为两个操作数。
    Binary {
        line: usize,
        op: TokenKind,
        left: Box<Node>,
        right: Box<Node>,
    },
    /// 函数调用（含内置 `喵叫`）：name 为函数名，args 为实参列表。
    Call {
        line: usize,
        name: String,
        args: Vec<Node>,
    },
    /// `如果 条件 那么? { } 否则? { }`，else 分支可空。
    If {
        line: usize,
        cond: Box<Node>,
        then: Box<Node>,
        else_b: Option<Box<Node>>,
    },
    /// `当 条件 那么? { }`。
    While {
        line: usize,
        cond: Box<Node>,
        body: Box<Node>,
    },
    /// `返回 表达式?`，无返回值时 value 为 None。
    Return {
        line: usize,
        value: Option<Box<Node>>,
    },
    /// 代码块：stmts 为语句列表。
    Block { line: usize, stmts: Vec<Node> },
    /// 程序（顶层语句列表，同 Block 用法）。
    Prog { stmts: Vec<Node> },
    /// 函数定义：`喵 名(形参*) { 体 }`。
    Func {
        line: usize,
        name: String,
        params: Vec<String>,
        body: Box<Node>,
    },
}

impl Node {
    /**
     * 返回节点类型（对应 C 版直接读 kind 字段）。
     *
     * @return 该节点对应的 NodeKind
     */
    pub fn kind(&self) -> NodeKind {
        match self {
            Node::Num { .. } => NodeKind::Num,
            Node::Str { .. } => NodeKind::Str,
            Node::Ident { .. } => NodeKind::Ident,
            Node::Var { .. } => NodeKind::Var,
            Node::Array { .. } => NodeKind::Array,
            Node::Assign { .. } => NodeKind::Assign,
            Node::Index { .. } => NodeKind::Index,
            Node::Binary { .. } => NodeKind::Binary,
            Node::Call { .. } => NodeKind::Call,
            Node::If { .. } => NodeKind::If,
            Node::While { .. } => NodeKind::While,
            Node::Return { .. } => NodeKind::Return,
            Node::Block { .. } => NodeKind::Block,
            Node::Prog { .. } => NodeKind::Prog,
            Node::Func { .. } => NodeKind::Func,
        }
    }

    /**
     * 返回节点在源码中的行号（用于报错定位）。
     *
     * 对应 C 版直接读 line 字段；Prog 无行号返回 0。
     *
     * @return 行号（从 1 开始；0 表示未知）
     */
    pub fn line(&self) -> usize {
        match self {
            Node::Num { line, .. }
            | Node::Str { line, .. }
            | Node::Ident { line, .. }
            | Node::Var { line, .. }
            | Node::Array { line, .. }
            | Node::Assign { line, .. }
            | Node::Index { line, .. }
            | Node::Binary { line, .. }
            | Node::Call { line, .. }
            | Node::If { line, .. }
            | Node::While { line, .. }
            | Node::Return { line, .. }
            | Node::Block { line, .. }
            | Node::Func { line, .. } => *line,
            Node::Prog { .. } => 0,
        }
    }
}

/**
 * 统一错误类型（对应 C 版 error_at：行号 + 消息）。
 *
 * C 版 error_at 是 noreturn（打印后 exit(1)）；Rust 版改为携带
 * (line, msg) 的错误值，由 main.rs 收尾统一格式化「第 N 行：消息」
 * 到 stderr 并 exit(1)，对外行为一致。
 *
 * @param line 源码行号（从 1 开始；0 表示未知行）
 * @param msg  错误描述文本
 */
#[derive(Debug, Clone)]
pub struct RuaError {
    pub line: usize,
    pub msg: String,
}

impl RuaError {
    /// 构造一个错误（line + 消息）。
    pub fn new(line: usize, msg: impl Into<String>) -> Self {
        RuaError {
            line,
            msg: msg.into(),
        }
    }
}

impl std::fmt::Display for RuaError {
    /// 格式化为「第 N 行：消息」（与 C 版 error_at 输出一致）。
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "第 {} 行：{}", self.line, self.msg)
    }
}

impl std::error::Error for RuaError {}

/// 全项目统一的结果类型：`Result<T> = Result<T, RuaError>`。
pub type Result<T> = std::result::Result<T, RuaError>;

/**
 * 构造错误（便捷宏，内部使用）。
 *
 * 用法：`rua_err!(line, "消息")` 返回一个 RuaError。
 * 对应 C 版 error_at 的报错入口，但不再直接退出进程。
 */
#[macro_export]
macro_rules! rua_err {
    ($line:expr, $msg:expr) => {
        $crate::ast::RuaError::new($line, $msg)
    };
}

/**
 * 返回 AST 节点类型的可读中文名（仅调试输出用，对应 C 版 node_kind_name）。
 *
 * @param k 节点类型
 * @return 中文字符串名
 */
pub fn node_kind_name(k: NodeKind) -> &'static str {
    match k {
        NodeKind::Num => "数字",
        NodeKind::Str => "字符串",
        NodeKind::Ident => "变量引用",
        NodeKind::Var => "变量声明",
        NodeKind::Array => "数组声明",
        NodeKind::Assign => "赋值",
        NodeKind::Index => "数组下标",
        NodeKind::Binary => "二元运算",
        NodeKind::Call => "函数调用",
        NodeKind::If => "如果",
        NodeKind::While => "当循环",
        NodeKind::Return => "返回",
        NodeKind::Block => "代码块",
        NodeKind::Prog => "程序",
        NodeKind::Func => "函数定义",
    }
}
