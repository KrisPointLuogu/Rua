// src/vm.rs — 字节码 VM（纯整数快路径）
//
// 对应 C 版 JIT 的「可编译判定」思路（includes/jit*.h），但用安全的栈机
// 字节码替代手写 x86-64 机器码，全程无 unsafe：
//   - 只编译「可 VM」的纯整数函数（只用形参/局部变量，无字符串/数组/全局引用）；
//   - 函数内支持 喵叫（整数与字符串字面量实参，对应 C 版 JIT 的打印支持）；
//   - 递归/互调通过函数索引表间接调用（对应 C 版两遍编译 + g_jit_table）；
//   - 其余函数与顶层语句继续由 runtime.rs 树遍历解释（对应 C 版
//     exec_call 按 f->jittable 分发）。
//
// 可 VM 判定（scan_*，对应 C 版 jit_plan_function 的可 JIT 判定）：
//   1. 节点类型受限：无 N_STR/N_ARRAY/N_INDEX（N_STR 仅允许作 喵叫 实参）；
//   2. 所有 N_IDENT 都必须是形参或函数内 `变量` 声明的局部变量
//      （引用全局变量 → 不可 VM）；
//   3. 被调用函数也必须可 VM（闭包传播，自底向上迭代，对应 C 版
//      canJIT 的闭包传递）。
//
// 与 C 版 JIT 的差异：
//   - 栈机没有寄存器参数上限（C 版 Linux ≤6 / Windows ≤4），放宽到 64；
//   - 除 0 报精确行号错误（C 版 JIT 除 0 是未定义行为）；
//   - 无显式 `返回` 的函数返回 0（与 C 版 JIT「无显式返回时清 RAX」一致）。

use std::collections::HashMap;
use std::rc::Rc;

use crate::ast::{Node, Result};
use crate::rua_err;

/**
 * VM 值：栈元素。
 *
 * 可 VM 函数内只有整数运算；字符串字面量仅作 喵叫 实参出现
 * （对应 C 版 JIT 的数据区字符串字面量）。
 */
#[derive(Debug, Clone)]
enum VmVal {
    /// 整数。
    Int(i64),
    /// 字符串字面量（共享句柄）。
    Str(Rc<String>),
}

impl VmVal {
    /**
     * 读取 C 版 Value 的 num 字段语义：字符串视为 0。
     *
     * @return 整数值，或 0（字符串）
     */
    fn as_int(&self) -> i64 {
        match self {
            VmVal::Int(n) => *n,
            VmVal::Str(_) => 0,
        }
    }
}

/**
 * 字节码指令（栈机）。
 *
 * 运算符指令从栈弹出右、左操作数，把结果压回栈顶；
 * 跳转目标是指令下标（不是字节偏移），由编译期局部回填。
 */
#[derive(Debug, Clone)]
enum Inst {
    /// 压入整数。
    Push(i64),
    /// 压入字符串字面量（仅 喵叫 实参）。
    PushStr(Rc<String>),
    /// 局部变量读入栈。
    LoadVar(u32),
    /// 栈顶写入局部变量。
    StoreVar(u32),
    /// 加法。
    Add,
    /// 减法。
    Sub,
    /// 乘法。
    Mul,
    /// 除法（携带源码行号用于报错）。
    Div(u32),
    /// 取模（携带源码行号用于报错）。
    Mod(u32),
    /// 相等。
    Eq,
    /// 不等。
    Ne,
    /// 大于。
    Gt,
    /// 小于。
    Lt,
    /// 大于等于。
    Ge,
    /// 小于等于。
    Le,
    /// 无条件跳转。
    Jmp(usize),
    /// 栈顶为假（0）则跳转。
    Jz(usize),
    /// 调用用户函数（VmEngine 索引 + 实参个数）。
    Call(usize, u8),
    /// 内置 喵叫：弹出 n 个实参打印。
    CallPrint(u8),
    /// 返回：弹出栈顶作为函数结果。
    Ret,
    /// 丢弃栈顶（表达式语句结果）。
    Pop,
}

/**
 * 一个已编译的 VM 函数。
 *
 * @param code   指令序列（Rc 共享，帧间复用避免深拷贝）
 * @param locals 局部槽位总数（形参 + 局部变量），运行期帧据此补 0
 */
#[derive(Debug)]
pub struct VmFunc {
    code: Rc<Vec<Inst>>,
    locals: u32,
}

/**
 * VM 引擎：函数索引表（对应 C 版 g_jit_table）。
 *
 * 只包含「可 VM」函数；函数名 → 索引映射供编译期引用与运行期分发。
 */
pub struct VmEngine {
    funcs: Vec<VmFunc>,
    map: HashMap<String, usize>,
}

impl VmEngine {
    /**
     * 按函数名查函数索引（None 表示该函数不可 VM）。
     *
     * @param name 函数名
     * @return 在 VmEngine 中的索引，或 None
     */
    pub fn get_idx(&self, name: &str) -> Option<usize> {
        self.map.get(name).copied()
    }

    /**
     * 按索引取函数名（调试输出用）。
     *
     * @param idx 函数索引
     * @return 函数名；越界返回 "?"
     */
    pub fn func_name(&self, idx: usize) -> &str {
        self.map
            .iter()
            .find(|(_, &v)| v == idx)
            .map(|(k, _)| k.as_str())
            .unwrap_or("?")
    }

    /// 可 VM 函数个数。
    pub fn len(&self) -> usize {
        self.funcs.len()
    }
}

/**
 * 函数预扫描信息（对应 C 版 jit_build_vars 的结果）。
 *
 * @param vars    局部变量表：名字 → 槽位（形参先占 0..P-1，N_VAR 按序续分）
 * @param callees 被调用函数名列表（用于闭包传播）
 */
pub(crate) struct ScanInfo {
    pub(crate) vars: HashMap<String, u32>,
    pub(crate) callees: Vec<String>,
}

/**
 * 对一个函数做可 VM 预扫描（共享判定入口）。
 *
 * JIT 用同一判定作为「可 JIT」的前置条件（可 JIT = 可 VM && 参数 ≤6 &&
 * 平台 x86-64）。通过返回局部变量表与调用列表；不通过返回 None。
 *
 * @param params 形参名列表
 * @param body   函数体（N_BLOCK）
 * @return 通过则返回预扫描信息，否则 None
 */
pub(crate) fn scan_function(params: &[String], body: &Node) -> Option<ScanInfo> {
    let mut info = ScanInfo {
        vars: HashMap::new(),
        callees: Vec::new(),
    };
    for (k, p) in params.iter().enumerate() {
        info.vars.insert(p.clone(), k as u32);
    }
    if scan_block(body, &mut info) {
        Some(info)
    } else {
        None
    }
}

/**
 * 编译期遍历状态。
 *
 * @param map  函数名 → VmEngine 索引（编译期引用被调函数）
 * @param vars 局部变量表（来自预扫描，编译与扫描槽位一致）
 * @param code 已生成的指令序列
 */
struct FnCompiler<'a> {
    map: &'a HashMap<String, usize>,
    vars: HashMap<String, u32>,
    code: Vec<Inst>,
}

impl<'a> FnCompiler<'a> {
    /// 追加一条指令，返回其下标（供跳转回填）。
    fn emit(&mut self, inst: Inst) -> usize {
        self.code.push(inst);
        self.code.len() - 1
    }

    /**
     * 把表达式编译为「求值结果入栈」的指令序列。
     *
     * 支持 N_NUM / N_STR（仅 喵叫 实参）/ N_IDENT / N_BINARY / N_CALL；
     * 其余节点类型在预扫描阶段已被拒绝，编译期仅作防御性报错。
     *
     * @param e 表达式节点
     */
    fn compile_expr(&mut self, e: &Node) -> Result<()> {
        match e {
            Node::Num { num, .. } => {
                self.emit(Inst::Push(*num));
            }
            Node::Str { text, .. } => {
                self.emit(Inst::PushStr(Rc::new(text.clone())));
            }
            Node::Ident { name, .. } => {
                let slot = *self
                    .vars
                    .get(name)
                    .ok_or_else(|| rua_err!(e.line(), "未定义的变量"))?;
                self.emit(Inst::LoadVar(slot));
            }
            Node::Binary {
                op,
                left,
                right,
                line,
            } => {
                self.compile_expr(left)?;
                self.compile_expr(right)?;
                let inst = match op {
                    crate::lexer::TokenKind::Plus => Inst::Add,
                    crate::lexer::TokenKind::Minus => Inst::Sub,
                    crate::lexer::TokenKind::Star => Inst::Mul,
                    crate::lexer::TokenKind::Slash => Inst::Div(*line as u32),
                    crate::lexer::TokenKind::Percent => Inst::Mod(*line as u32),
                    crate::lexer::TokenKind::Eqeq => Inst::Eq,
                    crate::lexer::TokenKind::Neq => Inst::Ne,
                    crate::lexer::TokenKind::Gt => Inst::Gt,
                    crate::lexer::TokenKind::Lt => Inst::Lt,
                    crate::lexer::TokenKind::Ge => Inst::Ge,
                    crate::lexer::TokenKind::Le => Inst::Le,
                    _ => return Err(rua_err!(*line, "未知运算符")),
                };
                self.emit(inst);
            }
            Node::Call { name, args, line } => {
                if name == "喵叫" {
                    // 内置打印：逐实参求值入栈，再统一弹出打印。
                    for a in args {
                        self.compile_expr(a)?;
                    }
                    self.emit(Inst::CallPrint(args.len() as u8));
                } else {
                    // 用户函数：实参入栈，Call 弹出 nargs 个放入新帧。
                    let idx = *self
                        .map
                        .get(name)
                        .ok_or_else(|| rua_err!(*line, "未定义的函数"))?;
                    for a in args {
                        self.compile_expr(a)?;
                    }
                    self.emit(Inst::Call(idx, args.len() as u8));
                }
            }
            _ => {
                return Err(rua_err!(e.line(), "不支持该表达式"));
            }
        }
        Ok(())
    }

    /**
     * 编译一条语句。
     *
     * keep = true 时该语句的求值结果保留在栈顶（供块/分支结果使用）；
     * keep = false 时结果丢弃（Pop）。
     *
     * 控制流：
     *   - N_IF：条件 -> Jz else；then 块；Jmp end；else 块；end。
     *     前向跳转先发占位（0），编译完块后按当前位置回填。
     *   - N_WHILE：loop: 条件 -> Jz end；body；Jmp loop；end。
     *   - N_RETURN：求值（无值补 0）-> Ret（直接结束函数帧）。
     *
     * @param s    语句节点
     * @param keep 是否保留语句结果在栈顶
     */
    fn compile_stmt(&mut self, s: &Node, keep: bool) -> Result<()> {
        match s {
            Node::Var { name, right, .. } => {
                self.compile_expr(right)?;
                let slot = *self
                    .vars
                    .get(name)
                    .ok_or_else(|| rua_err!(s.line(), "未定义的变量"))?;
                self.emit(Inst::StoreVar(slot));
                if !keep {
                    self.emit(Inst::Pop);
                }
            }
            Node::Assign { target, right, .. } => {
                let slot = match target.as_ref() {
                    Node::Ident { name, .. } => *self
                        .vars
                        .get(name)
                        .ok_or_else(|| rua_err!(target.line(), "未定义的变量"))?,
                    _ => return Err(rua_err!(target.line(), "赋值目标无效")),
                };
                self.compile_expr(right)?;
                self.emit(Inst::StoreVar(slot));
                if !keep {
                    self.emit(Inst::Pop);
                }
            }
            Node::Return { value, .. } => {
                match value {
                    Some(e) => {
                        self.compile_expr(e)?;
                    }
                    None => {
                        self.emit(Inst::Push(0));
                    }
                }
                self.emit(Inst::Ret);
            }
            Node::If {
                cond, then, else_b, ..
            } => {
                self.compile_expr(cond)?;
                let jz = self.emit(Inst::Jz(0));
                self.compile_block(then, keep)?;
                let jmp = self.emit(Inst::Jmp(0));
                let else_pos = self.code.len();
                match else_b {
                    Some(eb) => self.compile_block(eb, keep)?,
                    None => {
                        if keep {
                            self.emit(Inst::Push(0));
                        }
                    }
                }
                let end = self.code.len();
                self.code[jz] = Inst::Jz(else_pos);
                self.code[jmp] = Inst::Jmp(end);
            }
            Node::While { cond, body, .. } => {
                let loop_pos = self.code.len();
                self.compile_expr(cond)?;
                let jz = self.emit(Inst::Jz(0));
                self.compile_block(body, false)?;
                self.emit(Inst::Jmp(loop_pos));
                let end = self.code.len();
                self.code[jz] = Inst::Jz(end);
                if keep {
                    self.emit(Inst::Push(0));
                }
            }
            Node::Block { .. } => {
                self.compile_block(s, keep)?;
            }
            _ => {
                self.compile_expr(s)?;
                if !keep {
                    self.emit(Inst::Pop);
                }
            }
        }
        Ok(())
    }

    /**
     * 编译一个代码块：逐语句编译，最后一条语句按 keep 保留结果。
     *
     * 空块在 keep=true 时压入 0（无语句的块求值结果为 0）。
     *
     * @param b    代码块节点（N_BLOCK 或 N_PROG）
     * @param keep 是否把块结果保留在栈顶
     */
    fn compile_block(&mut self, b: &Node, keep: bool) -> Result<()> {
        let stmts = match b {
            Node::Block { stmts, .. } | Node::Prog { stmts } => stmts,
            _ => return Err(rua_err!(b.line(), "期望代码块")),
        };
        if stmts.is_empty() {
            if keep {
                self.emit(Inst::Push(0));
            }
            return Ok(());
        }
        let last = stmts.len() - 1;
        for s in &stmts[..last] {
            self.compile_stmt(s, false)?;
        }
        self.compile_stmt(&stmts[last], keep)
    }
}

/**
 * 表达式预扫描：判断是否可在 VM 内求值（纯整数），并收集调用。
 *
 * 对应 C 版 JIT 的节点受限判定（见文件头）。字符串字面量仅在 喵叫
 * 实参位置放行（N_STR 单独命中则返回 false）。
 *
 * @param e    表达式节点
 * @param info 预扫描信息（会被修改：登记 N_CALL 的 callees）
 * @return true 表示可在 VM 内求值
 */
fn scan_expr(e: &Node, info: &mut ScanInfo) -> bool {
    match e {
        Node::Num { .. } => true,
        Node::Str { .. } => false,
        Node::Ident { name, .. } => info.vars.contains_key(name),
        Node::Binary { left, right, .. } => scan_expr(left, info) && scan_expr(right, info),
        Node::Call { name, args, .. } => {
            if name == "喵叫" {
                for a in args {
                    match a {
                        // 字符串字面量实参允许（其余需是整数表达式）。
                        Node::Str { .. } => {}
                        _ => {
                            if !scan_expr(&a, info) {
                                return false;
                            }
                        }
                    }
                }
                true
            } else {
                info.callees.push(name.clone());
                for a in args {
                    if !scan_expr(a, info) {
                        return false;
                    }
                }
                true
            }
        }
        _ => false,
    }
}

/**
 * 语句预扫描：判定可 VM 并登记局部变量槽位。
 *
 * N_VAR 在求值完右式后登记名字 → 槽位（槽位按扫描顺序续分，与编译期
 * 的 StoreVar 槽位一致）；N_ASSIGN 的目标必须是已登记的局部变量
 * （引用全局/未声明 → 不可 VM）。
 *
 * @param s    语句节点
 * @param info 预扫描信息（会被修改：登记局部变量与 callees）
 * @return true 表示语句可 VM
 */
fn scan_stmt(s: &Node, info: &mut ScanInfo) -> bool {
    match s {
        Node::Var { name, right, .. } => {
            if !scan_expr(right, info) {
                return false;
            }
            let slot = info.vars.len() as u32;
            info.vars.insert(name.clone(), slot);
            true
        }
        Node::Assign { target, right, .. } => match target.as_ref() {
            Node::Ident { name, .. } => {
                if !info.vars.contains_key(name) {
                    return false;
                }
                scan_expr(right, info)
            }
            _ => false,
        },
        Node::Return { value, .. } => match value {
            Some(e) => scan_expr(e, info),
            None => true,
        },
        Node::If {
            cond, then, else_b, ..
        } => {
            if !scan_expr(cond, info) {
                return false;
            }
            if !scan_block(then, info) {
                return false;
            }
            match else_b {
                Some(e) => scan_block(e, info),
                None => true,
            }
        }
        Node::While { cond, body, .. } => scan_expr(cond, info) && scan_block(body, info),
        Node::Block { .. } => scan_block(s, info),
        _ => scan_expr(s, info),
    }
}

/**
 * 块预扫描（逐语句判定）。
 *
 * @param b    代码块节点（N_BLOCK 或 N_PROG）
 * @param info 预扫描信息（会被修改）
 * @return true 表示块内所有语句可 VM
 */
fn scan_block(b: &Node, info: &mut ScanInfo) -> bool {
    let stmts = match b {
        Node::Block { stmts, .. } | Node::Prog { stmts } => stmts,
        _ => return false,
    };
    stmts.iter().all(|s| scan_stmt(s, info))
}

/**
 * 提取函数定义节点的字段（name, params, body）。
 *
 * @param f 函数定义节点
 * @return 若为 N_FUNC 返回对应引用，否则 None
 */
fn func_parts(f: &Node) -> Option<(&str, &Vec<String>, &Node)> {
    match f {
        Node::Func {
            name, params, body, ..
        } => Some((name.as_str(), params, body)),
        _ => None,
    }
}

/**
 * 对函数表做两遍编译：先判定 + 闭包传播，再逐个生成字节码。
 *
 * 对应 C 版 jit_compile_all：
 *   1) 预扫描每个函数（可 VM 判定 + 局部变量表 + 收集 callees）；
 *   2) 闭包传播：调用不可 VM 函数 → 自己也改为不可 VM（迭代直到收敛）；
 *   3) 第一遍给可 VM 函数占位（索引按序分配，使递归/互调编译期可引用）；
 *   4) 第二遍逐个编译函数体（尾部自动补 Ret），写回字节码。
 *
 * @param fns 顶层函数定义节点引用列表
 * @return 只包含可 VM 函数的 VmEngine
 */
pub fn compile_functions(fns: &[&Node]) -> VmEngine {
    let n = fns.len();
    let mut scans: Vec<Option<ScanInfo>> = Vec::with_capacity(n);
    let mut name_to_idx: HashMap<String, usize> = HashMap::new();
    for (i, f) in fns.iter().enumerate() {
        if let Some((name, params, body)) = func_parts(f) {
            name_to_idx.insert(name.to_string(), i);
            scans.push(scan_function(params, body));
        } else {
            scans.push(None);
        }
    }

    // 闭包传播：调用不可 VM 函数（或表中不存在的函数）→ 自己也不可 VM。
    let mut eligible: Vec<bool> = scans.iter().map(|s| s.is_some()).collect();
    loop {
        let mut changed = false;
        for i in 0..n {
            if !eligible[i] {
                continue;
            }
            let info = scans[i].as_ref().unwrap();
            for callee in &info.callees {
                let bad = match name_to_idx.get(callee) {
                    Some(&j) => !eligible[j],
                    None => true,
                };
                if bad {
                    eligible[i] = false;
                    changed = true;
                    break;
                }
            }
        }
        if !changed {
            break;
        }
    }

    // 第一遍：给可 VM 函数占位（索引按序分配，供编译期引用）。
    let mut engine = VmEngine {
        funcs: Vec::new(),
        map: HashMap::new(),
    };
    for (i, f) in fns.iter().enumerate() {
        if eligible[i] {
            if let Some((name, _, _)) = func_parts(f) {
                let idx = engine.funcs.len();
                engine.map.insert(name.to_string(), idx);
                engine.funcs.push(VmFunc {
                    code: Rc::new(Vec::new()),
                    locals: 0,
                });
            }
        }
    }

    // 第二遍：逐个编译（函数体以 keep=true 编译，栈顶即隐式返回值；补 Ret）。
    for (i, f) in fns.iter().enumerate() {
        if !eligible[i] {
            continue;
        }
        let (name, _, body) = func_parts(f).unwrap();
        let info = scans[i].as_ref().unwrap();
        let vars = info.vars.clone();
        let mut c = FnCompiler {
            map: &engine.map,
            vars,
            code: Vec::new(),
        };
        if c.compile_block(body, true).is_ok() {
            c.emit(Inst::Ret);
        }
        let idx = engine.map[name];
        let total_locals = info.vars.len() as u32;
        engine.funcs[idx] = VmFunc {
            code: Rc::new(c.code),
            locals: total_locals,
        };
    }

    engine
}

/**
 * 执行帧。
 *
 * @param code   当前函数指令序列（Rc 共享）
 * @param pc     下一条指令下标
 * @param stack  操作数栈（VmVal）
 * @param locals 局部槽位（形参 + 局部变量，未初始化的补 0）
 */
struct Frame {
    code: Rc<Vec<Inst>>,
    pc: usize,
    stack: Vec<VmVal>,
    locals: Vec<VmVal>,
}

/**
 * 执行一个 VM 函数（入口），支持递归与互调。
 *
 * 用 Vec<Frame> 模拟调用栈：
 *   - Call：弹出实参（逆序还原）放入新帧 locals（补 0 到该函数槽位总数），
 *     压入当前帧，切到被调函数；
 *   - Ret：弹出栈顶作为函数结果，弹回调用方帧并把结果压回其栈顶；
 *     若调用栈为空则返回最终结果。
 * 除 0 / 模 0 报携带源码行号的错误（对应解释器的运行时错误）。
 *
 * @param engine VM 引擎
 * @param idx    入口函数索引
 * @param args   入口实参（整数）
 * @return 函数返回值
 */
pub fn invoke(engine: &VmEngine, idx: usize, args: &[i64]) -> crate::ast::Result<i64> {
    if idx >= engine.funcs.len() {
        return Err(rua_err!(0, "未定义的函数"));
    }
    let entry = &engine.funcs[idx];
    let mut frames: Vec<Frame> = Vec::new();
    let mut locals: Vec<VmVal> = args.iter().map(|&n| VmVal::Int(n)).collect();
    locals.resize(entry.locals as usize, VmVal::Int(0));
    let mut frame = Frame {
        code: entry.code.clone(),
        pc: 0,
        stack: Vec::new(),
        locals,
    };

    loop {
        if frame.pc >= frame.code.len() {
            break;
        }
        let inst = frame.code[frame.pc].clone();
        frame.pc += 1;
        match inst {
            // 字面量 / 变量。
            Inst::Push(n) => frame.stack.push(VmVal::Int(n)),
            Inst::PushStr(s) => frame.stack.push(VmVal::Str(s)),
            Inst::LoadVar(i) => {
                let v = frame
                    .locals
                    .get(i as usize)
                    .cloned()
                    .unwrap_or(VmVal::Int(0));
                frame.stack.push(v);
            }
            Inst::StoreVar(i) => {
                let v = frame.stack.pop().unwrap_or(VmVal::Int(0));
                if (i as usize) < frame.locals.len() {
                    frame.locals[i as usize] = v;
                }
            }

            // 算术（弹出右、左操作数，压入结果）。
            Inst::Add => {
                let b = frame.stack.pop().unwrap_or(VmVal::Int(0));
                let a = frame.stack.pop().unwrap_or(VmVal::Int(0));
                frame.stack.push(VmVal::Int(a.as_int() + b.as_int()));
            }
            Inst::Sub => {
                let b = frame.stack.pop().unwrap_or(VmVal::Int(0));
                let a = frame.stack.pop().unwrap_or(VmVal::Int(0));
                frame.stack.push(VmVal::Int(a.as_int() - b.as_int()));
            }
            Inst::Mul => {
                let b = frame.stack.pop().unwrap_or(VmVal::Int(0));
                let a = frame.stack.pop().unwrap_or(VmVal::Int(0));
                frame.stack.push(VmVal::Int(a.as_int() * b.as_int()));
            }
            Inst::Div(line) => {
                let b = frame.stack.pop().unwrap_or(VmVal::Int(0));
                let a = frame.stack.pop().unwrap_or(VmVal::Int(0));
                if b.as_int() == 0 {
                    return Err(rua_err!(line as usize, "不能除以 0"));
                }
                frame.stack.push(VmVal::Int(a.as_int() / b.as_int()));
            }
            Inst::Mod(line) => {
                let b = frame.stack.pop().unwrap_or(VmVal::Int(0));
                let a = frame.stack.pop().unwrap_or(VmVal::Int(0));
                if b.as_int() == 0 {
                    return Err(rua_err!(line as usize, "不能取模 0"));
                }
                frame.stack.push(VmVal::Int(a.as_int() % b.as_int()));
            }

            // 比较（压入 0/1）。
            Inst::Eq => {
                let b = frame.stack.pop().unwrap_or(VmVal::Int(0));
                let a = frame.stack.pop().unwrap_or(VmVal::Int(0));
                frame
                    .stack
                    .push(VmVal::Int((a.as_int() == b.as_int()) as i64));
            }
            Inst::Ne => {
                let b = frame.stack.pop().unwrap_or(VmVal::Int(0));
                let a = frame.stack.pop().unwrap_or(VmVal::Int(0));
                frame
                    .stack
                    .push(VmVal::Int((a.as_int() != b.as_int()) as i64));
            }
            Inst::Gt => {
                let b = frame.stack.pop().unwrap_or(VmVal::Int(0));
                let a = frame.stack.pop().unwrap_or(VmVal::Int(0));
                frame
                    .stack
                    .push(VmVal::Int((a.as_int() > b.as_int()) as i64));
            }
            Inst::Lt => {
                let b = frame.stack.pop().unwrap_or(VmVal::Int(0));
                let a = frame.stack.pop().unwrap_or(VmVal::Int(0));
                frame
                    .stack
                    .push(VmVal::Int((a.as_int() < b.as_int()) as i64));
            }
            Inst::Ge => {
                let b = frame.stack.pop().unwrap_or(VmVal::Int(0));
                let a = frame.stack.pop().unwrap_or(VmVal::Int(0));
                frame
                    .stack
                    .push(VmVal::Int((a.as_int() >= b.as_int()) as i64));
            }
            Inst::Le => {
                let b = frame.stack.pop().unwrap_or(VmVal::Int(0));
                let a = frame.stack.pop().unwrap_or(VmVal::Int(0));
                frame
                    .stack
                    .push(VmVal::Int((a.as_int() <= b.as_int()) as i64));
            }

            // 控制流。
            Inst::Jmp(t) => frame.pc = t,
            Inst::Jz(t) => {
                let v = frame.stack.pop().unwrap_or(VmVal::Int(0));
                if v.as_int() == 0 {
                    frame.pc = t;
                }
            }

            // 函数调用。
            Inst::Call(fidx, nargs) => {
                let mut argv = Vec::with_capacity(nargs as usize);
                for _ in 0..nargs {
                    argv.push(frame.stack.pop().unwrap_or(VmVal::Int(0)));
                }
                argv.reverse();
                if fidx >= engine.funcs.len() {
                    return Err(rua_err!(0, "未定义的函数"));
                }
                let callee = &engine.funcs[fidx];
                let mut locals = argv;
                locals.resize(callee.locals as usize, VmVal::Int(0));
                let new_frame = Frame {
                    code: callee.code.clone(),
                    pc: 0,
                    stack: Vec::new(),
                    locals,
                };
                frames.push(frame);
                frame = new_frame;
            }

            // 内置 喵叫：弹出 n 个实参，按序打印（空格分隔、末尾换行）。
            Inst::CallPrint(n) => {
                let mut vals = Vec::with_capacity(n as usize);
                for _ in 0..n {
                    vals.push(frame.stack.pop().unwrap_or(VmVal::Int(0)));
                }
                vals.reverse();
                for (i, v) in vals.iter().enumerate() {
                    if i > 0 {
                        print!(" ");
                    }
                    match v {
                        VmVal::Int(x) => print!("{}", x),
                        VmVal::Str(s) => print!("{}", s),
                    }
                }
                println!();
                frame.stack.push(VmVal::Int(0));
            }

            // 返回：栈顶为结果；调用栈为空则结束。
            Inst::Ret => {
                let r = frame.stack.pop().unwrap_or(VmVal::Int(0));
                if let Some(mut caller) = frames.pop() {
                    caller.stack.push(r);
                    frame = caller;
                } else {
                    return Ok(match r {
                        VmVal::Int(n) => n,
                        _ => 0,
                    });
                }
            }

            // 丢弃表达式语句结果。
            Inst::Pop => {
                frame.stack.pop();
            }
        }
    }

    // 防御：函数体末尾无 Ret（正常编译不会发生）。
    Ok(0)
}

impl Inst {
    /**
     * 把指令反汇编为可读文本（对应 C 版 jit_debug.h 的机器码反汇编）。
     *
     * @return 单条指令的文本表示
     */
    fn disasm(&self) -> String {
        match self {
            Inst::Push(n) => format!("push {}", n),
            Inst::PushStr(s) => format!("pushstr {:?}", s),
            Inst::LoadVar(i) => format!("load v{}", i),
            Inst::StoreVar(i) => format!("store v{}", i),
            Inst::Add => "add".to_string(),
            Inst::Sub => "sub".to_string(),
            Inst::Mul => "mul".to_string(),
            Inst::Div(l) => format!("div (line {})", l),
            Inst::Mod(l) => format!("mod (line {})", l),
            Inst::Eq => "eq".to_string(),
            Inst::Ne => "ne".to_string(),
            Inst::Gt => "gt".to_string(),
            Inst::Lt => "lt".to_string(),
            Inst::Ge => "ge".to_string(),
            Inst::Le => "le".to_string(),
            Inst::Jmp(t) => format!("jmp {}", t),
            Inst::Jz(t) => format!("jz {}", t),
            Inst::Call(i, n) => format!("call f{} ({} args)", i, n),
            Inst::CallPrint(n) => format!("call 喵叫 ({} args)", n),
            Inst::Ret => "ret".to_string(),
            Inst::Pop => "pop".to_string(),
        }
    }
}

/**
 * 把 VM 引擎的字节码转储为文本（--dump 用）。
 *
 * 对应 C 版 bytecode.h 的字节码文本转储（DEBUG 生效）。
 *
 * @param engine VM 引擎
 * @return 每函数的反汇编文本
 */
pub fn dump_engine(engine: &VmEngine) -> String {
    let mut out = String::new();
    out.push_str(&format!("可 VM 函数 {} 个\n", engine.funcs.len()));
    for (idx, f) in engine.funcs.iter().enumerate() {
        out.push_str(&format!(
            "函数[{}] {}：{} 条指令\n",
            idx,
            engine.func_name(idx),
            f.code.len()
        ));
        for (pc, inst) in f.code.iter().enumerate() {
            out.push_str(&format!("  {:>4}: {}\n", pc, inst.disasm()));
        }
    }
    out
}
