// src/runtime.rs — 运行时（树遍历解释器）
//
// 对应 C 版 includes/runtime.h。解释执行 AST：
//   - 值系统：数字 / 字符串 / 数组（Rust 枚举 Value，数组句柄共享）；
//   - 环境：作用域链（Env，Rc<RefCell>），函数调用时新建子环境；
//   - 求值：exec() 按节点类型递归求值，返回语句通过 Ctx.returning 向上传递；
//   - 内置二元运算符：按 TokenKind 分发（对应 C 版函数指针表 bin_table）；
//   - 函数调用：可 VM 的函数走字节码 VM（vm.rs），其余树遍历解释
//     （对应 C 版 exec_call 的 f->jittable 分发）。
//
// 与 C 版差异：
//   - C 版用空闲链表（env_freelist/binding_freelist）复用环境减少分配；
//     Rust 版由 Rc 自动管理生命周期，无需手工回收。
//   - C 版 env_get/env_set 沿指针链迭代；Rust 版用 Rc 链 + 借用（RefCell）
//     访问，语义一致（env_set 找不到时在当前环境新建绑定，隐式声明）。

use std::cell::RefCell;
use std::rc::Rc;

use crate::ast::{Node, NodeKind, Result, RuaError};
use crate::jit::{self, JitEngine};
use crate::lexer::TokenKind;
use crate::rua_err;
use crate::vm::{self, VmEngine};

/**
 * 运行时的值（对应 C 版标签联合 Value）。
 *
 * 按变体决定承载内容：
 *   Num 用 i64，Str 用 Rc<String>，Arr 用 Rc<RefCell<Vec<Value>>>。
 * 字符串/数组字段共享句柄，不进行深拷贝；数组还支持原地可变（句柄语义，
 * 对应 C 版 Array* 引用共享）。
 */
#[derive(Debug, Clone)]
pub enum Value {
    /// 数字（对应 C 版 V_NUM，long long）。
    Num(i64),
    /// 字符串（对应 C 版 V_STR，共享句柄）。
    Str(Rc<String>),
    /// 数组（对应 C 版 V_ARR：句柄共享 + 原地可变）。
    Arr(Rc<RefCell<Vec<Value>>>),
}

impl Value {
    /**
     * 读取 C 版 Value 的 num 字段语义。
     *
     * C 版字符串/数组的 num 字段恒为 0；Rust 版在需要「把值当数字」的
     * 场景（二元运算、VM 传参）沿用此规则。
     *
     * @return 数字值，或 0（字符串/数组）
     */
    fn num_field(&self) -> i64 {
        match self {
            Value::Num(n) => *n,
            _ => 0,
        }
    }
}

/**
 * 函数定义记录（函数表项，对应 C 版 Fn）。
 *
 * @param name   函数名
 * @param params 形参名列表
 * @param body   函数体（N_BLOCK 节点）
 * @param vm     该函数在 VmEngine 中的索引（None = 不可 VM，走树遍历解释）
 * @param jit    该函数在 JitEngine 中的索引（None = 不可 JIT）
 */
pub struct Function {
    pub name: String,
    pub params: Vec<String>,
    pub body: Node,
    pub vm: Option<usize>,
    pub jit: Option<usize>,
}

/**
 * 运行时：持有函数表、字节码 VM 引擎与 JIT 引擎，负责解释执行。
 *
 * 对应 C 版全局 fn_table / fn_count；Rust 版把全局状态收进结构体。
 * jit_enabled 对应 --jit/--no-jit 开关（默认 JIT 开）。
 */
pub struct Runtime {
    pub fns: Vec<Function>,
    pub vm: VmEngine,
    pub jit: JitEngine,
    pub jit_enabled: bool,
}

/**
 * 一个变量绑定（环境里的一项，对应 C 版 Binding）。
 *
 * @param name 变量名
 * @param val  变量当前值
 */
struct Binding {
    name: String,
    val: Value,
}

/**
 * 环境：一张作用域表，通过 parent 构成作用域链（对应 C 版 Env）。
 *
 * 函数调用时新建子环境（parent 指向调用者环境）；查找变量沿作用域链向上
 * （由内向外）。Rust 版用 Rc<RefCell> 表达共享可变，无 C 版的空闲链表。
 *
 * @param parent   父环境（外层作用域）
 * @param bindings 变量绑定数组
 */
pub struct Env {
    parent: Option<Rc<RefCell<Env>>>,
    bindings: Vec<Binding>,
}

/**
 * 求值上下文：一次函数调用（或程序运行）的状态（对应 C 版 Ctx）。
 *
 * @param env      当前作用域环境
 * @param returning 是否已遇到返回语句（true 表示需要结束当前函数）
 * @param retval    返回语句携带的返回值
 */
pub struct Ctx {
    env: Rc<RefCell<Env>>,
    returning: bool,
    retval: Value,
}

/**
 * 构造一个数字值（对应 C 版 num_val）。
 *
 * @param n 数值
 * @return 类型为 Num 的值
 */
pub fn num_val(n: i64) -> Value {
    Value::Num(n)
}

/**
 * 在当前环境绑定一个新变量（加入当前作用域，对应 C 版 env_bind）。
 *
 * @param e    目标环境
 * @param name 变量名
 * @param v    变量值
 */
fn env_bind(e: &mut Env, name: &str, v: Value) {
    e.bindings.push(Binding {
        name: name.to_string(),
        val: v,
    });
}

/**
 * 沿作用域链查找变量值（对应 C 版 env_get）。
 *
 * 递归向上查询，命中即返回该层变量值（浅拷贝句柄）；全部找不到报错。
 *
 * @param e    起始环境（Rc 句柄）
 * @param name 变量名
 * @return 最近一层作用域的变量值；找不到返回「未定义的变量」
 */
fn env_get(e: &Rc<RefCell<Env>>, name: &str) -> Result<Value> {
    let env = e.borrow();
    for b in &env.bindings {
        if b.name == name {
            return Ok(b.val.clone());
        }
    }
    match &env.parent {
        Some(p) => env_get(p, name),
        None => Err(rua_err!(0, "未定义的变量")),
    }
}

/**
 * 沿作用域链找到变量并修改其值（对应 C 版 env_set）。
 *
 * 先收集整条作用域链，逐层查找命中即改写；各层都找不到则在当前环境
 * （调用者的 env）新建绑定（隐式声明）。
 *
 * @param e    起始环境（Rc 句柄）
 * @param name 变量名
 * @param v    新值
 */
fn env_set(e: &Rc<RefCell<Env>>, name: &str, v: Value) {
    let mut chain = Vec::new();
    let mut cur = Some(e.clone());
    while let Some(c) = cur {
        let parent = c.borrow().parent.clone();
        chain.push(c);
        cur = parent;
    }
    for c in &chain {
        let mut env = c.borrow_mut();
        for b in &mut env.bindings {
            if b.name == name {
                b.val = v;
                return;
            }
        }
    }
    // 全链都找不到，在当前环境新建绑定（隐式声明）。
    let mut env = e.borrow_mut();
    env_bind(&mut env, name, v);
}

/**
 * 在函数表中按名字查找函数（对应 C 版 find_fn）。
 *
 * @param fns  函数表
 * @param name 函数名
 * @return 找到的函数记录；未找到返回 None
 */
fn find_fn<'a>(fns: &'a [Function], name: &str) -> Option<&'a Function> {
    fns.iter().find(|f| f.name == name)
}

/**
 * 判断一个值是否为真（用于条件判断，对应 C 版 is_true）。
 *
 * 数字非 0 为真；字符串非空为真；数组恒为真。
 *
 * @param v 待判断的值
 * @return true 为真
 */
fn is_true(v: &Value) -> bool {
    match v {
        Value::Str(s) => !s.is_empty(),
        Value::Arr(_) => true,
        Value::Num(n) => *n != 0,
    }
}

/**
 * 数组读取：arr[idx]（对应 C 版 array_get）。
 *
 * 越界或下标为负时抛运行时错误。
 *
 * @param base 数组句柄值
 * @param idx  下标值（必须是数字）
 * @param line 报错行号
 * @return 下标处的元素值
 */
fn array_get(base: &Value, idx: &Value, line: usize) -> Result<Value> {
    if let Value::Arr(arr) = base {
        let n = match idx {
            Value::Num(n) => *n,
            _ => return Err(rua_err!(line, "数组下标越界")),
        };
        if n < 0 {
            return Err(rua_err!(line, "数组下标越界"));
        }
        let items = arr.borrow();
        if (n as usize) >= items.len() {
            return Err(rua_err!(line, "数组下标越界"));
        }
        return Ok(items[n as usize].clone());
    }
    Err(rua_err!(line, "该变量不是数组"))
}

/**
 * 数组写入：arr[idx] = v（对应 C 版 array_set）。
 *
 * 越界或下标为负时抛运行时错误。
 *
 * @param base 数组句柄值
 * @param idx  下标值（必须是数字）
 * @param v    要写入的元素值
 * @param line 报错行号
 */
fn array_set(base: &Value, idx: &Value, v: Value, line: usize) -> Result<()> {
    if let Value::Arr(arr) = base {
        let n = match idx {
            Value::Num(n) => *n,
            _ => return Err(rua_err!(line, "数组下标越界")),
        };
        if n < 0 {
            return Err(rua_err!(line, "数组下标越界"));
        }
        let mut items = arr.borrow_mut();
        if (n as usize) >= items.len() {
            return Err(rua_err!(line, "数组下标越界"));
        }
        items[n as usize] = v;
        return Ok(());
    }
    Err(rua_err!(line, "该变量不是数组"))
}

/**
 * 求值下标读取表达式（N_INDEX 节点，对应 C 版 index_read）。
 *
 * 先求值基表达式与下标，再交给 array_get 完成读取；若基表达式是标识符
 * 且不是数组，提前报「该变量不是数组」（对应 C 版校验）。
 *
 * @param rt  运行时
 * @param n   N_INDEX 节点
 * @param ctx 求值上下文
 * @return 数组下标处的元素值
 */
fn index_read(rt: &Runtime, n: &Node, ctx: &mut Ctx) -> Result<Value> {
    let base = match n {
        Node::Index { base, index, line } => {
            let b = rt.exec(base, ctx)?;
            if base.kind() == NodeKind::Ident && !matches!(b, Value::Arr(_)) {
                return Err(rua_err!(*line, "该变量不是数组"));
            }
            let idx = rt.exec(index, ctx)?;
            array_get(&b, &idx, *line)?
        }
        _ => unreachable!(),
    };
    Ok(base)
}

/**
 * 把值写入赋值目标（对应 C 版 assign_target）。
 *
 * 目标是变量（N_IDENT）则修改环境变量；目标是下标（N_INDEX）则修改数组元素。
 *
 * @param rt     运行时
 * @param target 赋值目标节点（N_IDENT 或 N_INDEX）
 * @param v      新值
 * @param ctx    求值上下文
 */
fn assign_target(rt: &Runtime, target: &Node, v: Value, ctx: &mut Ctx) -> Result<()> {
    match target {
        Node::Ident { name, .. } => {
            env_set(&ctx.env, name, v);
        }
        Node::Index { base, index, line } => {
            let b = rt.exec(base, ctx)?;
            let idx = rt.exec(index, ctx)?;
            array_set(&b, &idx, v, *line)?;
        }
        _ => {
            return Err(rua_err!(target.line(), "赋值目标无效"));
        }
    }
    Ok(())
}

/**
 * 打印一个值到标准输出（不换行，对应 C 版 print_val）。
 *
 * 数字按 %lld 打印，字符串原样打印，数组打印「数组」。
 *
 * @param v 要打印的值
 */
fn print_val(v: &Value) {
    match v {
        Value::Str(s) => print!("{}", s),
        Value::Arr(_) => print!("数组"),
        Value::Num(n) => print!("{}", n),
    }
}

/**
 * 执行函数调用（N_CALL 节点，对应 C 版 exec_call）。
 *
 * 内置「喵叫」：逐参数求值并打印（参数间无分隔符），末尾换行。
 * 用户函数：先在函数表查名字、校验参数数量与上限（64），求值全部实参；
 *   若该函数已编译为 VM 字节码（f.vm 为 Some）则经 vm::invoke 调用；
 *   否则新建子环境绑定形参，在子上下文中求值函数体（无返回语句时返回
 *   函数体求值结果）。
 *
 * @param rt  运行时
 * @param n   N_CALL 节点
 * @param ctx 调用者的求值上下文
 * @return 调用结果
 */
fn exec_call(rt: &Runtime, n: &Node, ctx: &mut Ctx) -> Result<Value> {
    let (name, args, line) = match n {
        Node::Call { name, args, line } => (name.clone(), args, *line),
        _ => unreachable!(),
    };

    if name == "喵叫" {
        for (_i, arg) in args.iter().enumerate() {
            // if i > 0 {
            // print!(" ");
            // }
            let v = rt.exec(arg, ctx)?;
            print_val(&v);
        }
        println!();
        return Ok(num_val(0));
    }

    let f = find_fn(&rt.fns, &name).ok_or_else(|| rua_err!(line, "未定义的函数"))?;
    if f.params.len() != args.len() {
        return Err(rua_err!(line, "函数参数数量不匹配"));
    }
    if args.len() > 64 {
        return Err(rua_err!(line, "参数过多"));
    }

    let mut arg_vals = Vec::with_capacity(args.len());
    for a in args {
        arg_vals.push(rt.exec(a, ctx)?);
    }

    // JIT 快路径：可 JIT 时优先（仅整数实参，判定已保证）。
    if rt.jit_enabled {
        if let Some(idx) = f.jit {
            let mut argv = Vec::with_capacity(arg_vals.len());
            for a in &arg_vals {
                argv.push(a.num_field());
            }
            return Ok(num_val(jit::invoke(&rt.jit, idx, &argv)));
        }
    }

    // VM 快路径：只接受整数实参（可 VM 判定已保证）。
    if let Some(idx) = f.vm {
        let mut argv = Vec::with_capacity(arg_vals.len());
        for a in &arg_vals {
            argv.push(a.num_field());
        }
        return Ok(num_val(vm::invoke(&rt.vm, idx, &argv)?));
    }

    // 解释路径：新建子环境绑形参。
    let e = Rc::new(RefCell::new(Env {
        parent: Some(ctx.env.clone()),
        bindings: Vec::new(),
    }));
    {
        let mut env = e.borrow_mut();
        for (param, val) in f.params.iter().zip(arg_vals.iter()) {
            env_bind(&mut env, param, val.clone());
        }
    }

    let mut child = Ctx {
        env: e,
        returning: false,
        retval: num_val(0),
    };
    let r = rt.exec(&f.body, &mut child)?;
    Ok(if child.returning {
        child.retval.clone()
    } else {
        r
    })
}

impl Runtime {
    /**
     * 解释执行 AST 节点（主分发函数，对应 C 版 exec）。
     *
     * 按节点类型递归求值：
     *   - 字面量/标识符直接取值；
     *   - 语句（声明、赋值、分支、循环、返回）按语义执行；
     *   - 二元运算通过 apply_binop 分发；
     *   - 返回语句设置 ctx.returning，让循环/块提前结束并向上传递返回值。
     *
     * @param n   当前节点
     * @param ctx 求值上下文
     * @return 节点的求值结果
     */
    pub fn exec(&self, n: &Node, ctx: &mut Ctx) -> Result<Value> {
        match n {
            Node::Num { num, .. } => Ok(num_val(*num)),

            Node::Str { text, .. } => Ok(Value::Str(Rc::new(text.clone()))),

            Node::Ident { name, .. } => env_get(&ctx.env, name),

            Node::Var { name, right, .. } => {
                let v = self.exec(right, ctx)?;
                let mut env = ctx.env.borrow_mut();
                env_bind(&mut env, name, v.clone());
                Ok(v)
            }

            Node::Array {
                name, len, right, ..
            } => {
                let init = self.exec(right, ctx)?;
                let mut items = Vec::with_capacity(*len as usize);
                for _ in 0..*len {
                    items.push(init.clone());
                }
                let arr = Value::Arr(Rc::new(RefCell::new(items)));
                let mut env = ctx.env.borrow_mut();
                env_bind(&mut env, name, arr.clone());
                Ok(arr)
            }

            Node::Assign { target, right, .. } => {
                let v = self.exec(right, ctx)?;
                assign_target(self, target, v.clone(), ctx)?;
                Ok(v)
            }

            Node::Index { .. } => index_read(self, n, ctx),

            Node::Binary {
                op,
                left,
                right,
                line,
            } => {
                let a = self.exec(left, ctx)?;
                let b = self.exec(right, ctx)?;
                apply_binop(*op, &a, &b, *line)
            }

            Node::Call { .. } => exec_call(self, n, ctx),

            Node::If {
                cond, then, else_b, ..
            } => {
                let c = self.exec(cond, ctx)?;
                if is_true(&c) {
                    self.exec(then, ctx)
                } else if let Some(else_b) = else_b {
                    self.exec(else_b, ctx)
                } else {
                    Ok(num_val(0))
                }
            }

            Node::While { cond, body, .. } => {
                let mut last = num_val(0);
                loop {
                    let c = self.exec(cond, ctx)?;
                    if !is_true(&c) {
                        break;
                    }
                    last = self.exec(body, ctx)?;
                    if ctx.returning {
                        break;
                    }
                }
                Ok(last)
            }

            Node::Return { value, .. } => {
                let v = match value {
                    Some(e) => self.exec(e, ctx)?,
                    None => num_val(0),
                };
                ctx.returning = true;
                ctx.retval = v.clone();
                Ok(v)
            }

            Node::Func { .. } => Ok(num_val(0)),

            Node::Prog { stmts } | Node::Block { stmts, .. } => {
                let mut last = num_val(0);
                for s in stmts {
                    last = self.exec(s, ctx)?;
                    if ctx.returning {
                        break;
                    }
                }
                Ok(last)
            }
        }
    }

    /**
     * 程序入口：建全局环境并执行程序节点（对应 C 版 main 中
     * `env_new(NULL)` + `exec(prog, &ctx)`）。
     *
     * @param prog 程序节点（N_PROG）
     * @return 程序执行结果（通常为最后一条语句的值）
     */
    pub fn run(&self, prog: &Node) -> Result<Value> {
        let e = Rc::new(RefCell::new(Env {
            parent: None,
            bindings: Vec::new(),
        }));
        let mut ctx = Ctx {
            env: e,
            returning: false,
            retval: num_val(0),
        };
        self.exec(prog, &mut ctx)
    }

    /**
     * 构建运行时：把顶层 N_FUNC 节点编译成函数表，并预编译可 VM / 可 JIT 函数。
     *
     * 对应 C 版 register_fn + jit_compile_all：先给所有顶层函数在 VmEngine 与
     * JitEngine 中判定/编译（两遍），再为每个 Function 记录其 VM/JIT 索引
     * （None 表示不可编译，运行期走树遍历解释）。
     *
     * @param fns         顶层函数定义节点（N_FUNC）列表
     * @param jit_enabled 是否启用 JIT（--jit/--no-jit）
     * @return 组装好的运行时
     */
    pub fn build(fns: &[Node], jit_enabled: bool) -> Runtime {
        let refs: Vec<&Node> = fns.iter().collect();
        let engine = vm::compile_functions(&refs);
        let jit_engine = jit::compile_functions(&refs);
        let functions = fns
            .iter()
            .map(|f| {
                let (name, params, body) = match f {
                    Node::Func {
                        name, params, body, ..
                    } => (name.clone(), params.clone(), body.clone()),
                    _ => unreachable!(),
                };
                Function {
                    name: name.clone(),
                    params,
                    body: (*body).clone(),
                    vm: engine.get_idx(&name),
                    jit: jit_engine.get_idx(&name),
                }
            })
            .collect();
        Runtime {
            fns: functions,
            vm: engine,
            jit: jit_engine,
            jit_enabled,
        }
    }
}

/**
 * 把数字转成十进制字符串（对应 C 版 num_to_str，临时使用）。
 *
 * @param n 数值
 * @return 十进制字符串
 */
fn num_to_str(n: i64) -> String {
    n.to_string()
}

/**
 * 加法：数字相加，任一侧为字符串则按字符串拼接（对应 C 版 bin_add）。
 *
 * 字符串拼接时把另一侧的数字先转成字符串文本。
 *
 * @param a 左操作数
 * @param b 右操作数
 * @return 相加或拼接结果
 */
fn bin_add(a: &Value, b: &Value, _line: usize) -> Result<Value> {
    let a_is_str = matches!(a, Value::Str(_));
    let b_is_str = matches!(b, Value::Str(_));
    if a_is_str || b_is_str {
        let sa = match a {
            Value::Str(s) => s.as_str().to_string(),
            _ => num_to_str(a.num_field()),
        };
        let sb = match b {
            Value::Str(s) => s.as_str().to_string(),
            _ => num_to_str(b.num_field()),
        };
        Ok(Value::Str(Rc::new(format!("{}{}", sa, sb))))
    } else {
        Ok(num_val(a.num_field() + b.num_field()))
    }
}

/**
 * 减法（对应 C 版 bin_sub，要求操作数均为数字）。
 *
 * @param a 左操作数
 * @param b 右操作数
 * @return 相减结果
 */
fn bin_sub(a: &Value, b: &Value, line: usize) -> Result<Value> {
    if matches!(a, Value::Str(_)) || matches!(b, Value::Str(_)) {
        return Err(rua_err!(line, "运算需要数字"));
    }
    Ok(num_val(a.num_field() - b.num_field()))
}

/**
 * 乘法（对应 C 版 bin_mul，要求操作数均为数字）。
 *
 * @param a 左操作数
 * @param b 右操作数
 * @return 相乘结果
 */
fn bin_mul(a: &Value, b: &Value, line: usize) -> Result<Value> {
    if matches!(a, Value::Str(_)) || matches!(b, Value::Str(_)) {
        return Err(rua_err!(line, "运算需要数字"));
    }
    Ok(num_val(a.num_field() * b.num_field()))
}

/**
 * 除法：要求两个操作数都是数字，除数为 0 时报错（对应 C 版 bin_div）。
 *
 * @param a 左操作数
 * @param b 右操作数
 * @return 相除结果
 */
fn bin_div(a: &Value, b: &Value, line: usize) -> Result<Value> {
    if matches!(a, Value::Str(_)) || matches!(b, Value::Str(_)) {
        return Err(rua_err!(line, "运算需要数字"));
    }
    if b.num_field() == 0 {
        return Err(rua_err!(line, "不能除以 0"));
    }
    Ok(num_val(a.num_field() / b.num_field()))
}

/**
 * 取模：要求两个操作数都是数字，模数为 0 时报错（对应 C 版 bin_mod）。
 *
 * @param a 左操作数
 * @param b 右操作数
 * @return 取模结果
 */
fn bin_mod(a: &Value, b: &Value, line: usize) -> Result<Value> {
    if matches!(a, Value::Str(_)) || matches!(b, Value::Str(_)) {
        return Err(rua_err!(line, "运算需要数字"));
    }
    if b.num_field() == 0 {
        return Err(rua_err!(line, "不能取模 0"));
    }
    Ok(num_val(a.num_field() % b.num_field()))
}

/**
 * 判断两个值是否相等（对应 C 版 values_equal）。
 *
 * 两字符串比较内容；数字与数字比较数值；字符串与数字视为不相等。
 *
 * @param a 左操作数
 * @param b 右操作数
 * @return true 相等
 */
fn values_equal(a: &Value, b: &Value) -> bool {
    if matches!(a, Value::Str(_)) || matches!(b, Value::Str(_)) {
        match (a, b) {
            (Value::Str(x), Value::Str(y)) => x == y,
            _ => false,
        }
    } else {
        a.num_field() == b.num_field()
    }
}

/**
 * 等于运算（对应 C 版 bin_eq）。
 *
 * @param a 左操作数
 * @param b 右操作数
 * @return 1 相等，0 不相等
 */
fn bin_eq(a: &Value, b: &Value, _line: usize) -> Result<Value> {
    Ok(num_val(values_equal(a, b) as i64))
}

/**
 * 不等于运算（对应 C 版 bin_ne）。
 *
 * @param a 左操作数
 * @param b 右操作数
 * @return 1 不相等，0 相等
 */
fn bin_ne(a: &Value, b: &Value, _line: usize) -> Result<Value> {
    Ok(num_val(!values_equal(a, b) as i64))
}

/**
 * 比较运算（> < >= <=，对应 C 版 bin_gt/bin_lt/bin_ge/bin_le）。
 *
 * 要求两个操作数都是数字，否则报「比较需要数字」。
 *
 * @param a 左操作数
 * @param b 右操作数
 * @param line 报错行号
 * @param f    比较函数（x > y 等）
 * @return 1 成立，0 不成立
 */
fn bin_cmp(a: &Value, b: &Value, line: usize, f: fn(i64, i64) -> bool) -> Result<Value> {
    if matches!(a, Value::Str(_)) || matches!(b, Value::Str(_)) {
        return Err(rua_err!(line, "比较需要数字"));
    }
    Ok(num_val(f(a.num_field(), b.num_field()) as i64))
}

/**
 * 应用二元运算符（按 TokenKind 分发，对应 C 版 apply_binop + bin_table）。
 *
 * C 版用「函数指针表 bin_table + 下标分发」；Rust 版直接用 match 分发，
 * 效果等价（未指定的运算符报「未知运算符」）。
 *
 * @param op   运算符对应的 TokenKind
 * @param a    左操作数
 * @param b    右操作数
 * @param line 报错行号
 * @return 运算结果
 */
pub fn apply_binop(op: TokenKind, a: &Value, b: &Value, line: usize) -> Result<Value> {
    match op {
        TokenKind::Plus => bin_add(a, b, line),
        TokenKind::Minus => bin_sub(a, b, line),
        TokenKind::Star => bin_mul(a, b, line),
        TokenKind::Slash => bin_div(a, b, line),
        TokenKind::Percent => bin_mod(a, b, line),
        TokenKind::Eqeq => bin_eq(a, b, line),
        TokenKind::Neq => bin_ne(a, b, line),
        TokenKind::Gt => bin_cmp(a, b, line, |x, y| x > y),
        TokenKind::Lt => bin_cmp(a, b, line, |x, y| x < y),
        TokenKind::Ge => bin_cmp(a, b, line, |x, y| x >= y),
        TokenKind::Le => bin_cmp(a, b, line, |x, y| x <= y),
        _ => Err(RuaError::new(line, "未知运算符")),
    }
}
