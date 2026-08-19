// src/jit.rs — x86-64 JIT 编译器（纯整数快路径，AST 直编机器码）
//
// 对应 C 版 includes/jit*.h。把「可 JIT」的纯整数函数直接从 AST 编译成
// x86-64 机器码（SysV ABI），零第三方依赖（可执行内存用 extern "C" 直接
// 链接 libc 的 mmap）：
//   - 只编译「可 VM」的纯整数函数（共享 vm.rs 的 scan 判定），额外要求
//     参数 ≤6（SysV 寄存器参数上限）；
//   - 固定 vreg→物理寄存器映射：v0=RAX（结果）、v1..v6 形参（拷贝自 ABI
//     参数寄存器）、v7..v13 临时、v14+ 溢出到栈帧（[rbp-offset]）；
//   - 函数内支持 喵叫（整数经 jit_itoa 转文本、字符串字面量从数据区取
//     地址，经 jit_print 打印），对应 C 版 jit_print/jit_itoa；
//   - 递归/互调经函数地址表（g_jit_table）间接调用，两遍编译保证编译期
//     可引用（对应 C 版两遍编译）；
//   - 仅 Linux x86-64；其他平台 compile_functions 返回空引擎，函数回退 VM。
//
// 指令发射（照 C 版 jit_emit.h）：
//   mov r64,r64: REX.W 8B /r      mov r64,imm32: REX.W C7 /0
//   mov r64,imm64(movabs): REX.W B8+r
//   add: 03  sub: 2B  imul: 0F AF  cmp: 3B  test: 85
//   setcc al: 0F 90+cc C0  movzx eax,al: 0F B6 C0
//   cqo: REX.W 99  idiv: F7 /7  push: 50+r  pop: 58+r
//   sub rsp: 81 /5  add rsp: 81 /0  call reg: FF /2  ret: C3
//   jmp rel32: E9  jcc rel32: 0F 8x

use std::collections::HashMap;
use std::ffi::c_void;
use std::os::raw::{c_char, c_int};

use crate::ast::Node;
use crate::lexer::TokenKind;
use crate::vm;

// ==================== 物理寄存器号（x86-64 ModRM 编码） ====================

/// 物理寄存器编号（与 C 版 PhysReg 一致，供发射器直接使用）。
const RAX: u8 = 0;
const RCX: u8 = 1;
const RDX: u8 = 2;
const RBX: u8 = 3;
const RSP: u8 = 4;
const RBP: u8 = 5;
const RSI: u8 = 6;
const RDI: u8 = 7;
const R8: u8 = 8;
const R9: u8 = 9;
const R10: u8 = 10;
const R11: u8 = 11;
const R12: u8 = 12;
const R13: u8 = 13;
const R14: u8 = 14;
const R15: u8 = 15;

/// 固定 vreg→物理寄存器映射（v0..v13，SysV；对应 C 版 JIT_VREG_TO_PHYS）。
const VREG_TO_PHYS: [u8; 14] = [
    RAX, RBX, R12, R13, R14, R15, RSI, RDI, RDX, RCX, R8, R9, R10, R11,
];
/// ABI 参数寄存器（SysV：RDI/RSI/RDX/RCX/R8/R9）。
const ARG_REGS: [u8; 6] = [RDI, RSI, RDX, RCX, R8, R9];
/// 跨调用需保存的 caller-saved 寄存器（对应 C 版 JIT_SAVE_REGS）。
const SAVE_REGS: [u8; 8] = [RSI, RDI, RDX, RCX, R8, R9, R10, R11];
/// 需要序言 push / 尾声 pop 的 callee-saved 寄存器（SysV）。
const CALLEE_SAVED: [u8; 5] = [RBX, R12, R13, R14, R15];
/// 溢出起始 vreg：v14 及之后放入栈槽（对应 C 版 JIT_SPILL_VREG）。
const SPILL_VREG: u32 = 14;
/// 参数个数上限（SysV 寄存器参数上限）。
const JIT_MAX_PARAMS: usize = 6;

/// 条件码（0F 8x / 0F 9x 指令的低 4 位）。
const JCC_E: u8 = 4;
const JCC_NE: u8 = 5;
const JCC_L: u8 = 12;
const JCC_LE: u8 = 14;
const JCC_G: u8 = 15;
const JCC_GE: u8 = 13;

// ==================== 打印辅助（JIT 机器码调用，对应 C 版） ====================

/// 换行结束符（静态区，机器码取地址嵌入）。
static JIT_ENDL_NL: [u8; 2] = *b"\n\0";
/// 空格结束符。
static JIT_ENDL_SP: [u8; 2] = *b" \0";
/// 整数格式化临时缓冲（JIT 代码写入，对应 C 版 jit_scratch）。
static mut JIT_SCRATCH: [u8; 64] = [0u8; 64];

/**
 * 打印一段文本后接一个结束符。
 *
 * 由 JIT 生成的机器码按 SysV ABI 调用（RDI=文本，RSI=结束符）。
 */
#[no_mangle]
extern "C" fn jit_print(msg: *const c_char, endl: *const c_char) {
    let msg = unsafe { std::ffi::CStr::from_ptr(msg) };
    let endl = unsafe { std::ffi::CStr::from_ptr(endl) };
    print!("{}{}", msg.to_string_lossy(), endl.to_string_lossy());
}

/**
 * 把整数格式化成十进制文本（写入 buf）。
 *
 * 由 JIT 生成的机器码调用；与解释器打印数字格式一致（对应 C 版 jit_itoa）。
 */
#[no_mangle]
extern "C" fn jit_itoa(val: i64, buf: *mut c_char) -> *mut c_char {
    let s = val.to_string();
    unsafe {
        for (i, b) in s.bytes().enumerate() {
            *buf.add(i) = b as c_char;
        }
        *buf.add(s.len()) = 0;
    }
    buf
}

// ==================== 可执行内存（Linux mmap，零依赖） ====================

extern "C" {
    /// libc mmap（Rust 链接 glibc，无需 libc crate）。
    fn mmap(
        addr: *mut c_void,
        length: usize,
        prot: c_int,
        flags: c_int,
        fd: c_int,
        offset: isize,
    ) -> *mut c_void;
}

/// mmap 权限位（Linux asm/mman.h）。
const PROT_READ: c_int = 1;
const PROT_WRITE: c_int = 2;
const PROT_EXEC: c_int = 4;
/// mmap 标志位：私有匿名映射（不落盘）。
const MAP_PRIVATE: c_int = 2;
const MAP_ANONYMOUS: c_int = 0x20;
/// JIT 代码/数据缓冲容量（对应 C 版 1MB，不扩容不释放）。
const BUF_SIZE: usize = 1 << 20;

/**
 * 分配 RWX 匿名内存（对应 C 版 jit_alloc_exec）。
 *
 * @param size 字节数
 * @return 内存地址（失败返回 0）
 */
fn alloc_exec(size: usize) -> usize {
    unsafe {
        mmap(
            std::ptr::null_mut(),
            size,
            PROT_READ | PROT_WRITE | PROT_EXEC,
            MAP_PRIVATE | MAP_ANONYMOUS,
            -1,
            0,
        ) as usize
    }
}

// ==================== 指令发射器（照 C 版 jit_emit.h） ====================

/**
 * x86-64 指令字节发射器 + 标签回填。
 *
 * 机器码先写入 Vec<u8>（便于回填），编译完整体拷贝到可执行内存；
 * 标签与回填表也归发射器管理（避免跨结构体借用冲突）。
 */
struct Emitter {
    code: Vec<u8>,
    /// 标签 → 当前函数内代码偏移（-1 未绑定）。
    label_pos: Vec<i64>,
    /// 前向跳转回填表：(imm 字段偏移, 标签 id)。
    patches: Vec<(usize, u64)>,
}

impl Emitter {
    fn new() -> Emitter {
        Emitter {
            code: Vec::new(),
            label_pos: Vec::new(),
            patches: Vec::new(),
        }
    }

    /// 写入 1 字节。
    fn emit8(&mut self, v: u8) {
        self.code.push(v);
    }

    /// 写入 4 字节（小端）。
    fn emit32(&mut self, v: i32) {
        self.code.extend_from_slice(&v.to_le_bytes());
    }

    /// 写入 8 字节（小端）。
    fn emit64(&mut self, v: i64) {
        self.code.extend_from_slice(&v.to_le_bytes());
    }

    /// 发射 REX 前缀（W/R/X/B 位；全 0 时不发射）。
    fn rex(&mut self, w: bool, r: bool, x: bool, b: bool) {
        let v = 0x40 | ((w as u8) << 3) | ((r as u8) << 2) | ((x as u8) << 1) | (b as u8);
        if v != 0x40 {
            self.emit8(v);
        }
    }

    /// 发射 ModRM 字节。
    fn modrm(&mut self, modf: u8, oreg: u8, ereg: u8) {
        self.emit8((modf << 6) | ((oreg & 7) << 3) | (ereg & 7));
    }

    /// mov r64, r64（寄存器间拷贝；相同则跳过）。
    fn mov(&mut self, dst: u8, src: u8) {
        if dst == src {
            return;
        }
        self.rex(true, dst >= 8, false, src >= 8);
        self.emit8(0x8B);
        self.modrm(3, dst, src);
    }

    /// mov r64, imm32（符号扩展；必须用 C7 /0，见 C 版踩坑记录）。
    fn mov32(&mut self, reg: u8, imm: i32) {
        self.rex(true, false, false, reg >= 8);
        self.emit8(0xC7);
        self.modrm(3, 0, reg);
        self.emit32(imm);
    }

    /// mov r64, imm64（movabs，嵌入绝对地址）。
    fn mov64(&mut self, reg: u8, imm: i64) {
        self.rex(true, false, false, reg >= 8);
        self.emit8(0xB8 | (reg & 7));
        self.emit64(imm);
    }

    /// mov r64, [rbp+disp32]（读帧内槽位）。
    fn load_rbp(&mut self, reg: u8, disp: i32) {
        self.rex(true, reg >= 8, false, false);
        self.emit8(0x8B);
        self.modrm(2, reg, RBP);
        self.emit32(disp);
    }

    /// mov [rbp+disp32], r64（写帧内槽位）。
    fn store_rbp(&mut self, disp: i32, reg: u8) {
        self.rex(true, reg >= 8, false, false);
        self.emit8(0x89);
        self.modrm(2, reg, RBP);
        self.emit32(disp);
    }

    /// mov rax, [rax+disp32]（读函数地址表项）。
    fn load_rax_idx(&mut self, disp: i32) {
        self.rex(true, false, false, false);
        self.emit8(0x8B);
        self.modrm(2, RAX, RAX);
        self.emit32(disp);
    }

    /// add r64, r64。
    fn add(&mut self, dst: u8, src: u8) {
        self.rex(true, dst >= 8, false, src >= 8);
        self.emit8(0x03);
        self.modrm(3, dst, src);
    }

    /// sub r64, r64。
    fn sub(&mut self, dst: u8, src: u8) {
        self.rex(true, dst >= 8, false, src >= 8);
        self.emit8(0x2B);
        self.modrm(3, dst, src);
    }

    /// imul r64, r64。
    fn imul(&mut self, dst: u8, src: u8) {
        self.rex(true, dst >= 8, false, src >= 8);
        self.emit8(0x0F);
        self.emit8(0xAF);
        self.modrm(3, dst, src);
    }

    /// add r64, [rbp+disp32]。
    fn add_mem(&mut self, dst: u8, disp: i32) {
        self.rex(true, dst >= 8, false, false);
        self.emit8(0x03);
        self.modrm(2, dst, RBP);
        self.emit32(disp);
    }

    /// sub r64, [rbp+disp32]。
    fn sub_mem(&mut self, dst: u8, disp: i32) {
        self.rex(true, dst >= 8, false, false);
        self.emit8(0x2B);
        self.modrm(2, dst, RBP);
        self.emit32(disp);
    }

    /// imul r64, [rbp+disp32]。
    fn imul_mem(&mut self, dst: u8, disp: i32) {
        self.rex(true, dst >= 8, false, false);
        self.emit8(0x0F);
        self.emit8(0xAF);
        self.modrm(2, dst, RBP);
        self.emit32(disp);
    }

    /// cmp r64, r64。
    fn cmp(&mut self, a: u8, b: u8) {
        self.rex(true, a >= 8, false, b >= 8);
        self.emit8(0x3B);
        self.modrm(3, a, b);
    }

    /// cmp r64, [rbp+disp32]。
    fn cmp_mem(&mut self, a: u8, disp: i32) {
        self.rex(true, a >= 8, false, false);
        self.emit8(0x3B);
        self.modrm(2, a, RBP);
        self.emit32(disp);
    }

    /// test r64, r64（判零）。
    fn test(&mut self, reg: u8) {
        self.rex(true, false, false, reg >= 8);
        self.emit8(0x85);
        self.modrm(3, reg, reg);
    }

    /// setcc al。
    fn setcc_al(&mut self, cc: u8) {
        self.emit8(0x0F);
        self.emit8(0x90 | cc);
        self.emit8(0xC0);
    }

    /// movzx eax, al（零扩展到 RAX）。
    fn movzx_al(&mut self) {
        self.emit8(0x0F);
        self.emit8(0xB6);
        self.emit8(0xC0);
    }

    /// cqo（RAX 符号扩展到 RDX:RAX，供 idiv）。
    fn cqo(&mut self) {
        self.rex(true, false, false, false);
        self.emit8(0x99);
    }

    /// idiv r64（商→RAX，余数→RDX）。
    fn idiv_reg(&mut self, reg: u8) {
        self.rex(true, false, false, reg >= 8);
        self.emit8(0xF7);
        self.modrm(3, 7, reg);
    }

    /// idiv [rbp+disp32]。
    fn idiv_mem(&mut self, disp: i32) {
        self.rex(true, false, false, false);
        self.emit8(0xF7);
        self.modrm(2, 7, RBP);
        self.emit32(disp);
    }

    /// push r64。
    fn push(&mut self, reg: u8) {
        self.rex(false, false, false, reg >= 8);
        self.emit8(0x50 | (reg & 7));
    }

    /// pop r64。
    fn pop(&mut self, reg: u8) {
        self.rex(false, false, false, reg >= 8);
        self.emit8(0x58 | (reg & 7));
    }

    /// sub rsp, imm32（分配栈帧）。
    fn sub_rsp(&mut self, imm: i32) {
        self.rex(true, false, false, false);
        self.emit8(0x81);
        self.modrm(3, 5, RSP);
        self.emit32(imm);
    }

    /// add rsp, imm32（释放栈帧）。
    fn add_rsp(&mut self, imm: i32) {
        self.rex(true, false, false, false);
        self.emit8(0x81);
        self.modrm(3, 0, RSP);
        self.emit32(imm);
    }

    /// call r64（间接调用）。
    fn call_reg(&mut self, reg: u8) {
        self.rex(false, false, false, reg >= 8);
        self.emit8(0xFF);
        self.modrm(3, 2, reg);
    }

    /// xor eax, eax（清 RAX）。
    fn xor_eax(&mut self) {
        self.emit8(0x31);
        self.emit8(0xC0);
    }

    /// ret。
    fn ret(&mut self) {
        self.emit8(0xC3);
    }

    /// 新建标签，返回 id（未绑定）。
    fn new_label(&mut self) -> u64 {
        self.label_pos.push(-1);
        (self.label_pos.len() - 1) as u64
    }

    /// 把标签绑定到当前代码位置，并回填所有前向跳转。
    fn bind_label(&mut self, id: u64) {
        let pos = self.code.len() as i64;
        self.label_pos[id as usize] = pos;
        let mut i = 0;
        while i < self.patches.len() {
            if self.patches[i].1 == id {
                let (imm, _) = self.patches.remove(i);
                let off = (pos - (imm as i64 + 4)) as i32;
                self.code[imm..imm + 4].copy_from_slice(&off.to_le_bytes());
            } else {
                i += 1;
            }
        }
    }

    /// 回填或记录一次跳转（imm 字段已置 0）。
    fn patch_here(&mut self, imm: usize, id: u64) {
        let pos = self.label_pos[id as usize];
        if pos >= 0 {
            // 后向跳转：标签已绑定，直接写偏移。
            let off = (pos - (imm as i64 + 4)) as i32;
            self.code[imm..imm + 4].copy_from_slice(&off.to_le_bytes());
        } else {
            self.patches.push((imm, id));
        }
    }

    /// jmp rel32 → 标签。
    fn emit_jmp(&mut self, id: u64) {
        let p = self.code.len();
        self.emit8(0xE9);
        self.emit32(0);
        self.patch_here(p + 1, id);
    }

    /// jcc rel32 → 标签（条件跳转）。
    fn emit_jcc(&mut self, id: u64, cc: u8) {
        let p = self.code.len();
        self.emit8(0x0F);
        self.emit8(0x80 | cc);
        self.emit32(0);
        self.patch_here(p + 2, id);
    }
}

// ==================== 函数编译计划（照 C 版 jit_plan_function） ====================

/**
 * 单个函数的编译计划（代码生成依据）。
 *
 * @param param_count       形参个数
 * @param vars              变量名 → vreg（形参 v1..vP，局部按序续分）
 * @param max_vreg          用到的最大 vreg 号
 * @param needs_call        是否含调用/打印/除模（决定是否分配快照槽）
 * @param callee_saved_used 需要 push 的 callee-saved 物理寄存器
 * @param pushed_bytes      序言压栈字节（8 旧rbp + 8×npushed_callee）
 * @param frame_bytes       对齐后的帧大小（sub rsp 的量）
 * @param save_off          物理寄存器 → rbp 偏移（未保存为 0）
 */
struct FnPlan {
    param_count: u32,
    vars: HashMap<String, u32>,
    max_vreg: u32,
    needs_call: bool,
    callee_saved_used: [bool; 16],
    pushed_bytes: i32,
    frame_bytes: i32,
    save_off: [i32; 16],
}

impl FnPlan {
    /// vreg → 物理寄存器号；溢出（≥ SPILL_VREG）返回 None。
    fn phys(&self, vreg: u32) -> Option<u8> {
        if vreg < SPILL_VREG {
            Some(VREG_TO_PHYS[vreg as usize])
        } else {
            None
        }
    }

    /// 溢出槽相对 rbp 的偏移（vreg ≥ SPILL_VREG 时有效）。
    fn spill_off(&self, vreg: u32) -> i32 {
        -(self.pushed_bytes + (vreg as i32 - SPILL_VREG as i32) * 8)
    }
}

/**
 * 表达式 vreg 计数：推进 next（与代码生成同序），供帧布局计算。
 *
 * 节点类型合法性已由共享的 vm::scan_function 保证，这里只负责计数。
 */
fn count_expr(n: &Node, next: &mut u32) {
    match n {
        Node::Num { .. } | Node::Ident { .. } | Node::Str { .. } => {}
        Node::Binary { left, right, .. } => {
            *next += 2;
            count_expr(left, next);
            count_expr(right, next);
        }
        Node::Call { name, args, .. } => {
            *next += args.len() as u32;
            for a in args {
                if name == "喵叫" && matches!(a, Node::Str { .. }) {
                    continue;
                }
                count_expr(a, next);
            }
        }
        _ => {}
    }
}

/**
 * 语句 vreg 计数：推进 next（对应 C 版 jit_count_stmt）。
 */
fn count_stmt(n: &Node, next: &mut u32) {
    match n {
        Node::Var { right, .. } => {
            *next += 1;
            count_expr(right, next);
        }
        Node::Assign { right, .. } => {
            *next += 1;
            count_expr(right, next);
        }
        Node::Return { value, .. } => {
            *next += 1;
            if let Some(v) = value {
                count_expr(v, next);
            }
        }
        Node::If {
            cond, then, else_b, ..
        } => {
            *next += 1;
            count_expr(cond, next);
            count_stmt(then, next);
            if let Some(eb) = else_b {
                count_stmt(eb, next);
            }
        }
        Node::While { cond, body, .. } => {
            *next += 1;
            count_expr(cond, next);
            count_stmt(body, next);
        }
        Node::Block { stmts, .. } | Node::Prog { stmts } => {
            for s in stmts {
                count_stmt(s, next);
            }
        }
        _ => {
            *next += 1;
            count_expr(n, next);
        }
    }
}

/**
 * 登记局部变量名 → vreg（与 count/codegen 相同的遍历顺序与计数器推进）。
 * 对应 C 版 jit_build_vars：N_VAR 的 vreg = param_count + 1 + next。
 */
fn build_vars(n: &Node, next: &mut u32, plan: &mut FnPlan) {
    match n {
        Node::Var { name, right, .. } => {
            plan.vars.insert(name.clone(), plan.param_count + 1 + *next);
            *next += 1;
            count_expr(right, next);
        }
        Node::Assign { right, .. } => {
            *next += 1;
            count_expr(right, next);
        }
        Node::Return { value, .. } => {
            *next += 1;
            if let Some(v) = value {
                count_expr(v, next);
            }
        }
        Node::If {
            cond, then, else_b, ..
        } => {
            *next += 1;
            count_expr(cond, next);
            build_vars(then, next, plan);
            if let Some(eb) = else_b {
                build_vars(eb, next, plan);
            }
        }
        Node::While { cond, body, .. } => {
            *next += 1;
            count_expr(cond, next);
            build_vars(body, next, plan);
        }
        Node::Block { stmts, .. } | Node::Prog { stmts } => {
            for s in stmts {
                build_vars(s, next, plan);
            }
        }
        _ => {
            *next += 1;
            count_expr(n, next);
        }
    }
}

/**
 * 扫描函数是否含调用/打印/除模（决定是否分配 caller-saved 快照槽）。
 */
fn scan_side_effects(n: &Node, needs: &mut bool) {
    match n {
        Node::Call { .. } => *needs = true,
        Node::Binary {
            op, left, right, ..
        } => {
            if *op == TokenKind::Slash || *op == TokenKind::Percent {
                *needs = true;
            }
            scan_side_effects(left, needs);
            scan_side_effects(right, needs);
        }
        Node::Var { right, .. } => scan_side_effects(right, needs),
        Node::Assign { target, right, .. } => {
            scan_side_effects(target, needs);
            scan_side_effects(right, needs);
        }
        Node::Return { value, .. } => {
            if let Some(v) = value {
                scan_side_effects(v, needs);
            }
        }
        Node::If {
            cond, then, else_b, ..
        } => {
            scan_side_effects(cond, needs);
            scan_side_effects(then, needs);
            if let Some(eb) = else_b {
                scan_side_effects(eb, needs);
            }
        }
        Node::While { cond, body, .. } => {
            scan_side_effects(cond, needs);
            scan_side_effects(body, needs);
        }
        Node::Block { stmts, .. } | Node::Prog { stmts } => {
            for s in stmts {
                scan_side_effects(s, needs);
            }
        }
        _ => {}
    }
}

/**
 * 规划一个函数：共享判定 + 变量表 + 帧布局计算（对应 C 版 jit_plan_function）。
 *
 * 可 JIT = 可 VM（vm::scan_function 共享判定）&& 参数 ≤6。
 *
 * @param params 形参名列表
 * @param body   函数体（N_BLOCK）
 * @return 编译计划，不满足可 JIT 条件返回 None
 */
fn plan_function(params: &[String], body: &Node) -> Option<FnPlan> {
    if params.len() > JIT_MAX_PARAMS {
        return None;
    }
    // 共享 vm.rs 的判定：节点类型受限 + 变量引用合法 + 喵叫实参规则。
    if vm::scan_function(params, body).is_none() {
        return None;
    }

    let mut plan = FnPlan {
        param_count: params.len() as u32,
        vars: HashMap::new(),
        max_vreg: 0,
        needs_call: false,
        callee_saved_used: [false; 16],
        pushed_bytes: 0,
        frame_bytes: 0,
        save_off: [0; 16],
    };

    // 预置形参 vreg（v1..vP，对应 C 版 jit_var_add）。
    for (i, p) in params.iter().enumerate() {
        plan.vars.insert(p.clone(), (i + 1) as u32);
    }

    // 登记局部变量（与代码生成同序推进计数器）。
    let mut next = 0u32;
    build_vars(body, &mut next, &mut plan);

    // 计数得到 max_vreg。
    let mut cnext = 0u32;
    count_stmt(body, &mut cnext);
    let max_vreg = if cnext > 0 {
        params.len() as u32 + cnext
    } else {
        params.len() as u32
    };
    plan.max_vreg = max_vreg;

    // 用到的 vreg 对应的 callee-saved 物理寄存器。
    let mut npushed_callee = 0u32;
    for v in 1..=max_vreg {
        if let Some(p) = plan.phys(v) {
            if CALLEE_SAVED.contains(&p) {
                plan.callee_saved_used[p as usize] = true;
            }
        }
    }
    for &reg in CALLEE_SAVED.iter() {
        if plan.callee_saved_used[reg as usize] {
            npushed_callee += 1;
        }
    }
    plan.pushed_bytes = 8 + (npushed_callee as i32) * 8;

    // 溢出槽 + 快照槽。
    let spill_count = if max_vreg >= SPILL_VREG {
        max_vreg - (SPILL_VREG - 1)
    } else {
        0
    };
    let spill_bytes = spill_count as i32 * 8;
    let mut frame_bytes = spill_bytes;

    scan_side_effects(body, &mut plan.needs_call);
    if plan.needs_call {
        for (i, &reg) in SAVE_REGS.iter().enumerate() {
            plan.save_off[reg as usize] = -(plan.pushed_bytes + spill_bytes + (i as i32) * 8);
        }
        frame_bytes += (SAVE_REGS.len() as i32) * 8;
    }
    // SysV：入口 RSP ≡ 8 (mod 16)，内部 CALL 前需 ≡ 0 → (pushed+frame) ≡ 8。
    while (plan.pushed_bytes + frame_bytes) % 16 != 8 {
        frame_bytes += 8;
    }
    plan.frame_bytes = frame_bytes;

    Some(plan)
}

// ==================== 代码生成（照 C 版 jit_codegen.h） ====================

/**
 * JIT 编译状态：数据区（字符串字面量）+ 函数地址表引用 + 代码缓冲占用。
 */
struct JitState {
    /// 数据区内容（编译后拷贝到可执行数据缓冲）。
    data: Vec<u8>,
    /// 数据缓冲绝对地址（编译期已知，字面量地址据此计算）。
    data_base: usize,
    /// 函数地址表（g_jit_table）绝对地址。
    table_addr: usize,
    /// 函数名 → 地址表下标（emit_call 用）。
    table_idx: HashMap<String, usize>,
    /// 代码缓冲已用字节数（下一个函数从这里开始）。
    code_pos: usize,
}

/**
 * 单个函数的代码生成器状态。
 */
struct FnCompiler<'a> {
    jit: &'a mut JitState,
    plan: &'a FnPlan,
    em: Emitter,
    /// 尾声标签（N_RETURN 跳到此处）。
    epilogue: u64,
    /// 临时/局部 vreg 计数器（与 build_vars/count 同序推进）。
    next: u32,
}

impl<'a> FnCompiler<'a> {
    /// 把 vreg 的值加载到寄存器 dst。
    fn load_reg(&mut self, dst: u8, vreg: u32) {
        if vreg >= SPILL_VREG {
            self.em.load_rbp(dst, self.plan.spill_off(vreg));
        } else {
            let p = self.plan.phys(vreg).unwrap();
            if p != dst {
                self.em.mov(dst, p);
            }
        }
    }

    /// 把寄存器 src 的值存入 vreg。
    fn store_reg(&mut self, vreg: u32, src: u8) {
        if vreg >= SPILL_VREG {
            self.em.store_rbp(self.plan.spill_off(vreg), src);
        } else {
            let p = self.plan.phys(vreg).unwrap();
            if p != src {
                self.em.mov(p, src);
            }
        }
    }

    /// 调用参数装载：caller-saved 源一律从快照槽读（避免装载顺序互相覆盖）。
    fn load_arg_reg(&mut self, dst: u8, vreg: u32) {
        if vreg >= SPILL_VREG {
            self.em.load_rbp(dst, self.plan.spill_off(vreg));
        } else {
            let p = self.plan.phys(vreg).unwrap();
            if self.plan.save_off[p as usize] != 0 {
                self.em.load_rbp(dst, self.plan.save_off[p as usize]);
            } else if p != dst {
                self.em.mov(dst, p);
            }
        }
    }

    /// 把全部 caller-saved 寄存器存入帧内快照槽。
    fn save_callersaved(&mut self) {
        for &reg in SAVE_REGS.iter() {
            self.em.store_rbp(self.plan.save_off[reg as usize], reg);
        }
    }

    /// 从帧内快照槽恢复全部 caller-saved 寄存器。
    fn restore_callersaved(&mut self) {
        for &reg in SAVE_REGS.iter().rev() {
            self.em.load_rbp(reg, self.plan.save_off[reg as usize]);
        }
    }

    /// 字面量放入数据区，返回其绝对地址（同文本去重，对应 C 版 jit_lit_addr）。
    fn lit_addr(&mut self, text: &str) -> i64 {
        let mut base = 0usize;
        while base < self.jit.data.len() {
            let end = self.jit.data[base..]
                .iter()
                .position(|&b| b == 0)
                .map(|p| base + p)
                .unwrap_or(self.jit.data.len());
            if &self.jit.data[base..end] == text.as_bytes() {
                return (self.jit.data_base + base) as i64;
            }
            base = end + 1;
        }
        let off = self.jit.data.len();
        self.jit.data.extend_from_slice(text.as_bytes());
        self.jit.data.push(0);
        (self.jit.data_base + off) as i64
    }

    /// 把二元运算结果写入 dst vreg（照 C 版 jit_emit_binop）。
    fn emit_binop(&mut self, op: TokenKind, lt: u32, rt: u32, dst: u32) {
        match op {
            TokenKind::Plus | TokenKind::Minus | TokenKind::Star => {
                self.load_reg(RAX, lt);
                if rt >= SPILL_VREG {
                    let off = self.plan.spill_off(rt);
                    match op {
                        TokenKind::Plus => self.em.add_mem(RAX, off),
                        TokenKind::Minus => self.em.sub_mem(RAX, off),
                        _ => self.em.imul_mem(RAX, off),
                    }
                } else {
                    let r = self.plan.phys(rt).unwrap();
                    match op {
                        TokenKind::Plus => self.em.add(RAX, r),
                        TokenKind::Minus => self.em.sub(RAX, r),
                        _ => self.em.imul(RAX, r),
                    }
                }
                self.store_reg(dst, RAX);
            }
            TokenKind::Slash | TokenKind::Percent => {
                // idiv 破坏 RAX/RDX：先快照 caller-saved，算完恢复。
                self.save_callersaved();
                self.load_reg(RAX, lt);
                self.em.cqo();
                if rt >= SPILL_VREG {
                    self.em.idiv_mem(self.plan.spill_off(rt));
                } else {
                    self.em.idiv_reg(self.plan.phys(rt).unwrap());
                }
                if op == TokenKind::Percent {
                    self.em.mov(RAX, RDX);
                }
                self.restore_callersaved();
                self.store_reg(dst, RAX);
            }
            TokenKind::Eqeq
            | TokenKind::Neq
            | TokenKind::Lt
            | TokenKind::Le
            | TokenKind::Gt
            | TokenKind::Ge => {
                let cc = match op {
                    TokenKind::Eqeq => JCC_E,
                    TokenKind::Neq => JCC_NE,
                    TokenKind::Lt => JCC_L,
                    TokenKind::Le => JCC_LE,
                    TokenKind::Gt => JCC_G,
                    _ => JCC_GE,
                };
                self.load_reg(RAX, lt);
                if rt >= SPILL_VREG {
                    self.em.cmp_mem(RAX, self.plan.spill_off(rt));
                } else {
                    let r = self.plan.phys(rt).unwrap();
                    if r != RAX {
                        self.em.cmp(RAX, r);
                    }
                }
                self.em.setcc_al(cc);
                self.em.movzx_al();
                self.store_reg(dst, RAX);
            }
            _ => {}
        }
    }

    /// 发射一次调用：用户函数走地址表间接调用；喵叫 走 jit_print/jit_itoa。
    fn emit_call(&mut self, n: &Node, name: &str, args: &[Node], arg_vreg: &[u32], dst: u32) {
        let _ = n;
        if name == "喵叫" {
            self.save_callersaved();
            for (i, arg) in args.iter().enumerate() {
                if let Node::Str { text, .. } = arg {
                    let addr = self.lit_addr(text);
                    self.em.mov64(RDI, addr);
                } else {
                    self.load_arg_reg(RDI, arg_vreg[i]);
                    self.em
                        .mov64(RSI, unsafe { JIT_SCRATCH.as_ptr() as usize } as i64);
                    self.em.mov64(RAX, jit_itoa as usize as i64);
                    self.em.call_reg(RAX);
                    self.em
                        .mov64(RDI, unsafe { JIT_SCRATCH.as_ptr() as usize } as i64);
                }
                let endl = if i == args.len() - 1 {
                    JIT_ENDL_NL.as_ptr() as usize as i64
                } else {
                    JIT_ENDL_SP.as_ptr() as usize as i64
                };
                self.em.mov64(RSI, endl);
                self.em.mov64(RAX, jit_print as usize as i64);
                self.em.call_reg(RAX);
                self.restore_callersaved();
            }
            self.em.xor_eax();
            self.store_reg(dst, RAX);
            return;
        }

        let fidx = self.jit.table_idx[name];
        self.save_callersaved();
        for i in 0..args.len() {
            self.load_arg_reg(ARG_REGS[i], arg_vreg[i]);
        }
        self.em.mov64(RAX, self.jit.table_addr as i64);
        self.em.load_rax_idx((fidx as i32) * 8);
        self.em.call_reg(RAX);
        self.restore_callersaved();
        self.store_reg(dst, RAX);
    }

    /// 求值表达式到 dst vreg（照 C 版 jit_compile_expr）。
    fn compile_expr(&mut self, n: &Node, dst: u32) {
        match n {
            Node::Num { num, .. } => {
                let dstreg = match self.plan.phys(dst) {
                    Some(p) => p,
                    None => RAX,
                };
                if *num == (*num as i32) as i64 {
                    self.em.mov32(dstreg, *num as i32);
                } else {
                    self.em.mov64(dstreg, *num);
                }
                if dst >= SPILL_VREG {
                    self.em.store_rbp(self.plan.spill_off(dst), dstreg);
                }
            }
            Node::Ident { name, .. } => {
                let vreg = *self.plan.vars.get(name).expect("JIT 变量解析失败");
                let dstreg = match self.plan.phys(dst) {
                    Some(p) => p,
                    None => RAX,
                };
                self.load_reg(dstreg, vreg);
                if dst >= SPILL_VREG {
                    self.em.store_rbp(self.plan.spill_off(dst), dstreg);
                }
            }
            Node::Binary {
                op, left, right, ..
            } => {
                let lt = self.next + self.plan.param_count + 1;
                self.next += 1;
                let rt = self.next + self.plan.param_count + 1;
                self.next += 1;
                self.compile_expr(left, lt);
                self.compile_expr(right, rt);
                self.emit_binop(*op, lt, rt, dst);
            }
            Node::Call { name, args, .. } => {
                let mut arg_vreg = Vec::with_capacity(args.len());
                for _ in 0..args.len() {
                    arg_vreg.push(self.next + self.plan.param_count + 1);
                    self.next += 1;
                }
                for (i, arg) in args.iter().enumerate() {
                    if name == "喵叫" {
                        if let Node::Str { text, .. } = arg {
                            let addr = self.lit_addr(text);
                            let reg = match self.plan.phys(arg_vreg[i]) {
                                Some(p) => p,
                                None => RAX,
                            };
                            self.em.mov64(reg, addr);
                            if arg_vreg[i] >= SPILL_VREG {
                                self.em.store_rbp(self.plan.spill_off(arg_vreg[i]), reg);
                            }
                            continue;
                        }
                    }
                    self.compile_expr(arg, arg_vreg[i]);
                }
                self.emit_call(n, name, args, &arg_vreg, dst);
            }
            Node::Str { .. } => {}
            _ => {}
        }
    }

    /// 语句代码生成（照 C 版 jit_compile_stmt）。
    fn compile_stmt(&mut self, n: &Node) {
        match n {
            Node::Var { name, right, .. } => {
                let vreg = *self.plan.vars.get(name).expect("JIT 变量分配失败");
                self.next += 1;
                self.compile_expr(right, vreg);
            }
            Node::Assign { target, right, .. } => {
                let vreg = match target.as_ref() {
                    Node::Ident { name, .. } => {
                        *self.plan.vars.get(name).expect("JIT 赋值目标未找到")
                    }
                    _ => return,
                };
                self.next += 1;
                self.compile_expr(right, vreg);
            }
            Node::Return { value, .. } => {
                let t = self.next + self.plan.param_count + 1;
                self.next += 1;
                if let Some(v) = value {
                    self.compile_expr(v, t);
                    self.load_reg(RAX, t);
                }
                self.em.emit_jmp(self.epilogue);
            }
            Node::If {
                cond, then, else_b, ..
            } => {
                let c = self.next + self.plan.param_count + 1;
                self.next += 1;
                self.compile_expr(cond, c);
                self.load_reg(RAX, c);
                self.em.test(RAX);
                let else_lab = self.em.new_label();
                let done_lab = self.em.new_label();
                self.em.emit_jcc(else_lab, JCC_E);
                self.compile_stmt(then);
                self.em.emit_jmp(done_lab);
                self.em.bind_label(else_lab);
                if let Some(eb) = else_b {
                    self.compile_stmt(eb);
                }
                self.em.bind_label(done_lab);
            }
            Node::While { cond, body, .. } => {
                let loop_lab = self.em.new_label();
                let done_lab = self.em.new_label();
                self.em.bind_label(loop_lab);
                let c = self.next + self.plan.param_count + 1;
                self.next += 1;
                self.compile_expr(cond, c);
                self.load_reg(RAX, c);
                self.em.test(RAX);
                self.em.emit_jcc(done_lab, JCC_E);
                self.compile_stmt(body);
                self.em.emit_jmp(loop_lab);
                self.em.bind_label(done_lab);
            }
            Node::Block { stmts, .. } | Node::Prog { stmts } => {
                for s in stmts {
                    self.compile_stmt(s);
                }
            }
            _ => {
                let t = self.next + self.plan.param_count + 1;
                self.next += 1;
                self.compile_expr(n, t);
            }
        }
    }
}

// ==================== JIT 引擎（两遍编译 + 函数表） ====================

/**
 * JIT 引擎：函数地址表 + 名字索引（对应 C 版 g_jit_table）。
 *
 * 只包含「可 JIT」函数；运行期经函数地址表间接调用（递归/互调正确）。
 */
pub struct JitEngine {
    /// 函数地址表（g_jit_table）：下标 → 机器码入口地址。
    entries: Vec<usize>,
    /// 函数名 → entries 下标。
    map: HashMap<String, usize>,
    /// 可执行代码缓冲基址（持有防止被释放）。
    _code_base: usize,
    /// 数据缓冲基址（持有防止被释放）。
    _data_base: usize,
}

impl JitEngine {
    /// 按函数名查 JIT 索引（None 表示该函数不可 JIT）。
    pub fn get_idx(&self, name: &str) -> Option<usize> {
        self.map.get(name).copied()
    }

    /// 可 JIT 函数个数。
    pub fn len(&self) -> usize {
        self.entries.len()
    }
}

/**
 * 对函数表做两遍编译（对应 C 版 jit_compile_all）。
 *
 *   1) 逐个函数 plan（共享判定 + 参数 ≤6）→ 可 JIT 集合；
 *   2) 闭包传播：调用不可 JIT 函数 → 自己也改为不可 JIT；
 *   3) 第一遍给可 JIT 函数在地址表中占位（使递归/互调编译期可引用）；
 *   4) 第二遍逐个发射机器码，写回地址表与可执行内存。
 *
 * @param fns 顶层函数定义节点引用列表
 * @return 只包含可 JIT 函数的 JitEngine（平台不支持时为空引擎）
 */
pub fn compile_functions(fns: &[&Node]) -> JitEngine {
    let empty = || JitEngine {
        entries: Vec::new(),
        map: HashMap::new(),
        _code_base: 0,
        _data_base: 0,
    };
    if !cfg!(all(target_arch = "x86_64", target_os = "linux")) {
        return empty();
    }

    // 提取函数字段。
    let parts: Vec<(&str, &Vec<String>, &Node)> = fns
        .iter()
        .map(|f| match f {
            Node::Func {
                name, params, body, ..
            } => (name.as_str(), params, body.as_ref()),
            _ => unreachable!(),
        })
        .collect();

    // 1. 逐个 plan。
    let mut plans: Vec<Option<FnPlan>> = Vec::with_capacity(parts.len());
    for (_, params, body) in &parts {
        plans.push(plan_function(params, body));
    }

    // 2. 闭包传播：调用不可 JIT → 自己也不可 JIT。
    let mut name_to_idx: HashMap<&str, usize> = HashMap::new();
    for (i, (name, _, _)) in parts.iter().enumerate() {
        name_to_idx.insert(name, i);
    }
    let mut eligible: Vec<bool> = plans.iter().map(|p| p.is_some()).collect();
    loop {
        let mut changed = false;
        for i in 0..parts.len() {
            if !eligible[i] {
                continue;
            }
            let mut callees = Vec::new();
            collect_callees(parts[i].2, &mut callees);
            for callee in &callees {
                let bad = match name_to_idx.get(callee.as_str()) {
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

    // 3. 分配索引，建立地址表。
    let mut entries: Vec<usize> = Vec::new();
    let mut map: HashMap<String, usize> = HashMap::new();
    for (i, (name, _, _)) in parts.iter().enumerate() {
        if eligible[i] {
            let idx = entries.len();
            map.insert(name.to_string(), idx);
            entries.push(0);
        }
    }
    if entries.is_empty() {
        return empty();
    }

    // 4. 分配可执行内存。
    let data_base = alloc_exec(BUF_SIZE);
    let code_base = alloc_exec(BUF_SIZE);
    if data_base == 0 || code_base == 0 {
        return empty();
    }
    let table_addr = entries.as_ptr() as usize;
    let mut jit = JitState {
        data: Vec::new(),
        data_base,
        table_addr,
        table_idx: map.clone(),
        code_pos: 0,
    };

    // 5. 逐个发射函数。
    for (i, (name, params, body)) in parts.iter().enumerate() {
        if !eligible[i] {
            continue;
        }
        let plan = plans[i].as_ref().unwrap();
        let idx = map[*name];
        entries[idx] = emit_function(&mut jit, plan, params, body, code_base);
    }

    // 6. 把数据区（字符串字面量）拷贝到可执行数据缓冲（地址编译期已嵌入）。
    if !jit.data.is_empty() {
        let dst = unsafe { std::slice::from_raw_parts_mut(data_base as *mut u8, jit.data.len()) };
        dst.copy_from_slice(&jit.data);
    }

    JitEngine {
        entries,
        map,
        _code_base: code_base,
        _data_base: data_base,
    }
}

/// 收集函数体内被调用的非喵叫函数名（闭包传播用）。
fn collect_callees(n: &Node, out: &mut Vec<String>) {
    match n {
        Node::Call { name, args, .. } => {
            if name != "喵叫" {
                out.push(name.clone());
            }
            for a in args {
                collect_callees(a, out);
            }
        }
        Node::Binary { left, right, .. } => {
            collect_callees(left, out);
            collect_callees(right, out);
        }
        Node::Var { right, .. } => collect_callees(right, out),
        Node::Assign { right, .. } => collect_callees(right, out),
        Node::Return { value, .. } => {
            if let Some(v) = value {
                collect_callees(v, out);
            }
        }
        Node::If {
            cond, then, else_b, ..
        } => {
            collect_callees(cond, out);
            collect_callees(then, out);
            if let Some(eb) = else_b {
                collect_callees(eb, out);
            }
        }
        Node::While { cond, body, .. } => {
            collect_callees(cond, out);
            collect_callees(body, out);
        }
        Node::Block { stmts, .. } | Node::Prog { stmts } => {
            for s in stmts {
                collect_callees(s, out);
            }
        }
        _ => {}
    }
}

/**
 * 编译单个函数（照 C 版 jit_emit_function）。
 *
 * 序言：push rbp → mov rbp,rsp → push callee-saved → sub rsp,frame → 清 RAX
 *       → 形参从 ABI 寄存器拷到各自 vreg；
 * 函数体：逐语句生成；
 * 尾声：加回 frame → 逆序 pop callee-saved → pop rbp → ret。
 * 机器码整体拷贝到代码缓冲，返回入口绝对地址。
 */
fn emit_function(
    jit: &mut JitState,
    plan: &FnPlan,
    params: &[String],
    body: &Node,
    code_base: usize,
) -> usize {
    let mut comp = FnCompiler {
        jit,
        plan,
        em: Emitter::new(),
        epilogue: 0,
        next: 0,
    };
    let epilogue = comp.em.new_label();
    comp.epilogue = epilogue;

    // 序言。
    comp.em.push(RBP);
    comp.em.mov(RBP, RSP);
    for &reg in CALLEE_SAVED.iter() {
        if comp.plan.callee_saved_used[reg as usize] {
            comp.em.push(reg);
        }
    }
    if comp.plan.frame_bytes > 0 {
        comp.em.sub_rsp(comp.plan.frame_bytes);
    }
    comp.em.xor_eax();

    // 形参从 ABI 参数寄存器拷到各自 vreg（v1..vP）。
    for i in 0..params.len() {
        let phys = comp.plan.phys((i + 1) as u32).unwrap();
        if phys != ARG_REGS[i] {
            comp.em.mov(phys, ARG_REGS[i]);
        }
    }

    // 函数体。
    comp.compile_stmt(body);

    // 尾声。
    comp.em.bind_label(epilogue);
    if comp.plan.frame_bytes > 0 {
        comp.em.add_rsp(comp.plan.frame_bytes);
    }
    for &reg in CALLEE_SAVED.iter().rev() {
        if comp.plan.callee_saved_used[reg as usize] {
            comp.em.pop(reg);
        }
    }
    comp.em.pop(RBP);
    comp.em.ret();

    // 拷贝到可执行内存，返回入口地址。
    let code = std::mem::replace(&mut comp.em.code, Vec::new());
    let entry = code_base + jit.code_pos;
    let dst = unsafe { std::slice::from_raw_parts_mut(entry as *mut u8, code.len()) };
    dst.copy_from_slice(&code);
    jit.code_pos += code.len();
    entry
}

/**
 * 执行一个 JIT 函数（入口），按 SysV 调用约定调机器码。
 *
 * @param engine JIT 引擎
 * @param idx    函数索引
 * @param args   实参（整数）
 * @return 函数返回值
 */
pub fn invoke(engine: &JitEngine, idx: usize, args: &[i64]) -> i64 {
    type JitFn = unsafe extern "C" fn(i64, i64, i64, i64, i64, i64) -> i64;
    let entry = engine.entries[idx];
    let f: JitFn = unsafe { std::mem::transmute(entry) };
    let mut a = [0i64; 6];
    for (i, v) in args.iter().enumerate() {
        if i < 6 {
            a[i] = *v;
        }
    }
    unsafe { f(a[0], a[1], a[2], a[3], a[4], a[5]) }
}
