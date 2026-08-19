// src/debug.rs — 调试输出与字节码转储
//
// 对应 C 版 includes/debug_process.h（调试输出与计时）与
// includes/bytecode.h（字节码文本转储）。
//
// 由 main.rs 的 --debug / --dump 参数启用，全部输出到 stderr，
// 便于观察词法、AST、函数表与 VM 字节码结构，不污染程序 stdout。

use std::time::Instant;

use crate::ast::{node_kind_name, Node};
use crate::jit;
use crate::lexer::{token_kind_name, Token};
use crate::runtime::Function;
use crate::vm;

/**
 * 阶段计时器：drop 时打印耗时（对应 C 版 debug_process.h 的计时宏）。
 *
 * 用 drop 的时机控制计时段落（如 `let t = Timer::new(...); ...; drop(t);`）。
 */
pub struct Timer {
    name: &'static str,
    start: Instant,
}

impl Timer {
    /**
     * 开始计时。
     *
     * @param name 阶段名（打印时显示）
     */
    pub fn new(name: &'static str) -> Timer {
        Timer {
            name,
            start: Instant::now(),
        }
    }
}

impl Drop for Timer {
    /// drop 时打印「阶段名：xxx 秒」到 stderr。
    fn drop(&mut self) {
        eprintln!(
            "[调试] {}：{:.3} 秒",
            self.name,
            self.start.elapsed().as_secs_f64()
        );
    }
}

/**
 * 逐 Token 明细（对应 C 版 debug_dump_tokens）。
 *
 * 每行：下标、行号、类型中文名、文本（如有）、数值（如有）。
 *
 * @param toks Token 数组
 */
pub fn dump_tokens(toks: &[Token]) {
    eprintln!("[调试] 词法分析：共 {} 个 Token", toks.len());
    for (i, t) in toks.iter().enumerate() {
        match &t.text {
            Some(text) => eprintln!(
                "  [{}] 行 {} {} {:?} 数值={}",
                i,
                t.line,
                token_kind_name(t.kind),
                text,
                t.num
            ),
            None => eprintln!(
                "  [{}] 行 {} {} 数值={}",
                i,
                t.line,
                token_kind_name(t.kind),
                t.num
            ),
        }
    }
}

/**
 * 递归打印 AST 节点（对应 C 版 debug_print_stmts）。
 *
 * 按节点类型打印中文名与关键字段，子树缩进递归。
 *
 * @param n      当前节点
 * @param indent 缩进层级
 */
pub fn dump_node(n: &Node, indent: usize) {
    let pad = "  ".repeat(indent);
    match n {
        Node::Num { num, .. } => eprintln!("{}{} {}", pad, node_kind_name(n.kind()), num),
        Node::Str { text, .. } => eprintln!("{}{} {:?}", pad, node_kind_name(n.kind()), text),
        Node::Ident { name, .. } => eprintln!("{}{} {}", pad, node_kind_name(n.kind()), name),
        Node::Var { name, right, .. } => {
            eprintln!("{}{} {}", pad, node_kind_name(n.kind()), name);
            dump_node(right, indent + 1);
        }
        Node::Array {
            name, len, right, ..
        } => {
            eprintln!("{}{} {} 长度={}", pad, node_kind_name(n.kind()), name, len);
            dump_node(right, indent + 1);
        }
        Node::Assign { target, right, .. } => {
            eprintln!("{}赋值", pad);
            dump_node(target, indent + 1);
            dump_node(right, indent + 1);
        }
        Node::Index { base, index, .. } => {
            eprintln!("{}数组下标", pad);
            dump_node(base, indent + 1);
            dump_node(index, indent + 1);
        }
        Node::Binary {
            op, left, right, ..
        } => {
            eprintln!("{}二元运算 {:?}", pad, op);
            dump_node(left, indent + 1);
            dump_node(right, indent + 1);
        }
        Node::Call { name, args, .. } => {
            eprintln!("{}函数调用 {}", pad, name);
            for a in args {
                dump_node(a, indent + 1);
            }
        }
        Node::If {
            cond, then, else_b, ..
        } => {
            eprintln!("{}如果", pad);
            dump_node(cond, indent + 1);
            eprintln!("{}  真分支", pad);
            dump_node(then, indent + 1);
            if let Some(eb) = else_b {
                eprintln!("{}  假分支", pad);
                dump_node(eb, indent + 1);
            }
        }
        Node::While { cond, body, .. } => {
            eprintln!("{}当循环", pad);
            dump_node(cond, indent + 1);
            dump_node(body, indent + 1);
        }
        Node::Return { value, .. } => {
            eprintln!("{}返回", pad);
            if let Some(v) = value {
                dump_node(v, indent + 1);
            }
        }
        Node::Block { stmts, .. } | Node::Prog { stmts } => {
            eprintln!(
                "{}{}（{} 条语句）",
                pad,
                node_kind_name(n.kind()),
                stmts.len()
            );
            for s in stmts {
                dump_node(s, indent + 1);
            }
        }
        Node::Func {
            name, params, body, ..
        } => {
            eprintln!(
                "{}{} {}（形参 {:?}）",
                pad,
                node_kind_name(n.kind()),
                name,
                params
            );
            dump_node(body, indent + 1);
        }
    }
}

/**
 * 打印程序顶层结构（对应 C 版 debug_print_stmts 的入口）。
 *
 * @param prog 程序节点（N_PROG）
 */
pub fn dump_prog(prog: &Node) {
    eprintln!("[调试] 程序结构：");
    dump_node(prog, 1);
}

/**
 * 打印函数表与可 VM / 可 JIT 状态（对应 C 版 debug_print_fns）。
 *
 * 每个函数列出形参，并标注可 VM / 可 JIT 或解释执行（--debug 的表格输出）。
 *
 * @param fns        运行时函数表
 * @param engine     VM 引擎（用于统计可 VM 函数数）
 * @param jit        JIT 引擎（用于统计可 JIT 函数数）
 * @param jit_enabled 是否启用 JIT
 */
pub fn dump_functions(
    fns: &[Function],
    engine: &vm::VmEngine,
    jit: &jit::JitEngine,
    jit_enabled: bool,
) {
    eprintln!("[调试] 注册函数 {} 个", fns.len());
    for f in fns {
        let jit_state = if !jit_enabled {
            "已关闭".to_string()
        } else if f.jit.is_some() {
            format!("JIT [{}]", f.jit.unwrap())
        } else {
            "未 JIT".to_string()
        };
        let vm_state = if f.vm.is_some() {
            format!("VM [{}]", f.vm.unwrap())
        } else {
            "解释".to_string()
        };
        eprintln!(
            "  函数 {}（形参 {:?}）→ {} / {}",
            f.name, f.params, jit_state, vm_state
        );
    }
    eprintln!(
        "[调试] 可 JIT 函数 {} 个 / 可 VM 函数 {} 个",
        jit.len(),
        engine.len()
    );
}

/**
 * 转储 VM 字节码到 stderr（--dump，对应 C 版 debug_dump_bytecode）。
 *
 * @param engine VM 引擎
 */
pub fn dump_bytecode(engine: &vm::VmEngine) {
    eprintln!("[调试] 字节码转储：");
    let text = vm::dump_engine(engine);
    for line in text.lines() {
        eprintln!("  {}", line);
    }
}
