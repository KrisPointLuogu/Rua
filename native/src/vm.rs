//! 寄存器式解释器 —— 对齐 C++ `VM::run` 语义
//!
//! 架构（与 C++ 一致）：
//!   值栈（stack）—— 存放所有函数帧，每帧是一组连续寄存器
//!   帧基址（fp）  —— 当前帧在值栈中的起始位置
//!   寄存器 rX = stack[fp + X]
//!
//! 调用约定：
//!   CALL 前先 PUSH 参数（含 r0 占位），CALL 将 PUSH 的值作为新帧 r0..rN
//!   RET 将返回值写回调用者 r0
//!   JMP/JIF 的 extra 是相对偏移：ip += 8 + extra

use crate::rab::{self, Program};

pub const STACK_CAP: usize = 65536;
pub const CALL_CAP: usize = 65536;
pub const FRAME_CAP: usize = 65536;

#[derive(Clone, Copy, Debug, PartialEq)]
pub enum ValueType {
    Integer,
    String,
    Array,
}

#[derive(Clone, Copy, Debug)]
pub struct Value {
    pub ty: ValueType,
    pub data: i32,
}

impl Default for Value {
    fn default() -> Self {
        Value { ty: ValueType::Integer, data: 0 }
    }
}

impl Value {
    fn integer(v: i32) -> Self {
        Value { ty: ValueType::Integer, data: v }
    }
}

#[derive(Debug)]
pub struct VmError {
    pub message: String,
}

impl VmError {
    fn new(msg: impl Into<String>) -> Self {
        VmError { message: msg.into() }
    }
}

/// 直接解析 4 字节小端 i32（不校验越界，由外层保证 ip 有效）
#[inline]
fn extra_i32(code: &[u8], ip: usize) -> i32 {
    i32::from_le_bytes([
        code[ip + 4],
        code[ip + 5],
        code[ip + 6],
        code[ip + 7],
    ])
}

/// 单次执行入口。每次调用使用全新状态（与 C++ 每次 new VM 一致）。
pub fn run(program: &Program) -> Result<(), VmError> {
    let mut vm = VM::new(program);
    vm.execute()
}

struct VM<'a> {
    program: &'a Program,
    stack: Vec<Value>,
    call_stack: Vec<i32>,
    frame_stack: Vec<i32>,
    sp: i32,   // 值栈指针
    cp: i32,   // 调用栈指针
    fsp: i32,  // 帧栈指针
    fp: i32,   // 当前帧基址
    ip: i32,   // 指令指针
    str_pool: Vec<String>,
    array_pool: Vec<Vec<Value>>,
}

impl<'a> VM<'a> {
    fn new(program: &'a Program) -> Self {
        VM {
            program,
            stack: vec![Value::default(); STACK_CAP],
            call_stack: Vec::with_capacity(CALL_CAP),
            frame_stack: Vec::with_capacity(FRAME_CAP),
            sp: -1,
            cp: -1,
            fsp: -1,
            fp: 0,
            ip: 0,
            str_pool: Vec::new(),
            array_pool: Vec::new(),
        }
    }

    #[inline]
    fn reg(&self, r: i32) -> Value {
        self.stack[(self.fp + r) as usize]
    }

    #[inline]
    fn reg_mut(&mut self, r: i32) -> &mut Value {
        let idx = (self.fp + r) as usize;
        &mut self.stack[idx]
    }

    fn execute(&mut self) -> Result<(), VmError> {
        let code = &self.program.code;
        if code.is_empty() {
            return Err(VmError::new("字节码为空"));
        }
        let constants = &self.program.constants;
        let strings = &self.program.strings;
        let functions = &self.program.functions;
        let trace = std::env::var("RUA_TRACE").is_ok();
        let mut steps: i64 = 0;

        loop {
            if trace {
                steps += 1;
                if steps > 2000 {
                    eprintln!("[TRACE] 超过 2000 步，终止（疑似死循环）");
                    return Err(VmError::new("trace 步数超限"));
                }
            }
            let ip = self.ip as usize;
            let op = code[ip];
            if trace {
                eprintln!(
                    "[TRACE] ip={:4} op=0x{:02X} sp={} fp={} fsp={} cp={}",
                    ip, op, self.sp, self.fp, self.fsp, self.cp
                );
                if op == rab::OP_MOVI || op == rab::OP_SUB
                    || op == rab::OP_LE || op == rab::OP_JIF
                {
                    let rd = code[ip + 1];
                    let rs1 = code[ip + 2];
                    let rs2 = code[ip + 3];
                    eprintln!(
                        "[TRACE]     rd={} rs1={}(={}) rs2={}(={}) extra={}",
                        rd, rs1, self.reg(rs1 as i32).data, rs2,
                        self.reg(rs2 as i32).data, extra_i32(code, ip)
                    );
                }
            }
            match op {
                rab::OP_HALT => break,

                rab::OP_MOVI => {
                    let rd = code[ip + 1] as i32;
                    let ci = extra_i32(code, ip) as usize;
                    *self.reg_mut(rd) = Value::integer(constants[ci]);
                    self.ip += 8;
                }

                rab::OP_MOVS => {
                    let rd = code[ip + 1] as i32;
                    let si = extra_i32(code, ip) as usize;
                    self.str_pool.push(strings[si].clone());
                    let idx = (self.str_pool.len() - 1) as i32;
                    *self.reg_mut(rd) = Value { ty: ValueType::String, data: idx };
                    self.ip += 8;
                }

                rab::OP_MOV => {
                    let rd = code[ip + 1] as i32;
                    let rs = code[ip + 2] as i32;
                    let v = self.reg(rs);
                    *self.reg_mut(rd) = v;
                    self.ip += 8;
                }

                rab::OP_ADD => {
                    let rd = code[ip + 1] as i32;
                    let rs1 = code[ip + 2] as i32;
                    let rs2 = code[ip + 3] as i32;
                    let b = self.reg(rs1);
                    let c = self.reg(rs2);
                    if b.ty == ValueType::Integer && c.ty == ValueType::Integer {
                        *self.reg_mut(rd) = Value::integer(b.data + c.data);
                    } else if b.ty == ValueType::String
                        && c.ty == ValueType::String
                    {
                        let s = self.str_pool[b.data as usize].clone()
                            + &self.str_pool[c.data as usize];
                        self.str_pool.push(s);
                        let idx = (self.str_pool.len() - 1) as i32;
                        *self.reg_mut(rd) = Value { ty: ValueType::String, data: idx };
                    } else {
                        return Err(VmError::new("加法操作数类型不匹配"));
                    }
                    self.ip += 8;
                }

                rab::OP_SUB => {
                    let rd = code[ip + 1] as i32;
                    let rs1 = code[ip + 2] as i32;
                    let rs2 = code[ip + 3] as i32;
                    let v = self.reg(rs1).data - self.reg(rs2).data;
                    *self.reg_mut(rd) = Value::integer(v);
                    self.ip += 8;
                }

                rab::OP_MUL => {
                    let rd = code[ip + 1] as i32;
                    let rs1 = code[ip + 2] as i32;
                    let rs2 = code[ip + 3] as i32;
                    let v = self.reg(rs1).data * self.reg(rs2).data;
                    *self.reg_mut(rd) = Value::integer(v);
                    self.ip += 8;
                }

                rab::OP_DIV => {
                    let rd = code[ip + 1] as i32;
                    let rs1 = code[ip + 2] as i32;
                    let rs2 = code[ip + 3] as i32;
                    let c = self.reg(rs2).data;
                    if c == 0 {
                        return Err(VmError::new("除数为零"));
                    }
                    *self.reg_mut(rd) = Value::integer(self.reg(rs1).data / c);
                    self.ip += 8;
                }

                rab::OP_MOD => {
                    let rd = code[ip + 1] as i32;
                    let rs1 = code[ip + 2] as i32;
                    let rs2 = code[ip + 3] as i32;
                    let c = self.reg(rs2).data;
                    if c == 0 {
                        return Err(VmError::new("除数为零"));
                    }
                    *self.reg_mut(rd) = Value::integer(self.reg(rs1).data % c);
                    self.ip += 8;
                }

                rab::OP_EQ | rab::OP_NE => {
                    let rd = code[ip + 1] as i32;
                    let rs1 = code[ip + 2] as i32;
                    let rs2 = code[ip + 3] as i32;
                    let b = self.reg(rs1);
                    let c = self.reg(rs2);
                    let equal = if b.ty != c.ty {
                        false
                    } else if b.ty == ValueType::String {
                        self.str_pool[b.data as usize]
                            == self.str_pool[c.data as usize]
                    } else {
                        b.data == c.data
                    };
                    let result = if op == rab::OP_EQ {
                        if equal { 1 } else { 0 }
                    } else {
                        if equal { 0 } else { 1 }
                    };
                    *self.reg_mut(rd) = Value::integer(result);
                    self.ip += 8;
                }

                rab::OP_LT => {
                    let rd = code[ip + 1] as i32;
                    let rs1 = code[ip + 2] as i32;
                    let rs2 = code[ip + 3] as i32;
                    let v = if self.reg(rs1).data < self.reg(rs2).data { 1 } else { 0 };
                    *self.reg_mut(rd) = Value::integer(v);
                    self.ip += 8;
                }

                rab::OP_GT => {
                    let rd = code[ip + 1] as i32;
                    let rs1 = code[ip + 2] as i32;
                    let rs2 = code[ip + 3] as i32;
                    let v = if self.reg(rs1).data > self.reg(rs2).data { 1 } else { 0 };
                    *self.reg_mut(rd) = Value::integer(v);
                    self.ip += 8;
                }

                rab::OP_LE => {
                    let rd = code[ip + 1] as i32;
                    let rs1 = code[ip + 2] as i32;
                    let rs2 = code[ip + 3] as i32;
                    let v = if self.reg(rs1).data <= self.reg(rs2).data { 1 } else { 0 };
                    *self.reg_mut(rd) = Value::integer(v);
                    self.ip += 8;
                }

                rab::OP_GE => {
                    let rd = code[ip + 1] as i32;
                    let rs1 = code[ip + 2] as i32;
                    let rs2 = code[ip + 3] as i32;
                    let v = if self.reg(rs1).data >= self.reg(rs2).data { 1 } else { 0 };
                    *self.reg_mut(rd) = Value::integer(v);
                    self.ip += 8;
                }

                rab::OP_JMP => {
                    let offset = extra_i32(code, ip);
                    self.ip += 8 + offset;
                }

                rab::OP_JIF => {
                    let rs1 = code[ip + 2] as i32;
                    let offset = extra_i32(code, ip);
                    if self.reg(rs1).data == 0 {
                        self.ip += 8 + offset;
                    } else {
                        self.ip += 8;
                    }
                }

                rab::OP_PUSH => {
                    let rs1 = code[ip + 2] as i32;
                    let v = self.reg(rs1);
                    self.sp += 1;
                    self.stack[self.sp as usize] = v;
                    self.ip += 8;
                }

                rab::OP_CALL => {
                    let func_idx = extra_i32(code, ip) as usize;
                    let p_cnt = functions[func_idx].param_count as i32;
                    let r_cnt = functions[func_idx].reg_count as i32;

                    let new_fp = self.sp - p_cnt;
                    self.fsp += 1;
                    self.frame_stack.push(new_fp);

                    // 清零局部变量与临时区（newFP+pCnt+1 .. newFP+rCnt-1）
                    let dst_start = new_fp + p_cnt + 1;
                    let zero_count = r_cnt - p_cnt - 1;
                    for i in (0..zero_count).rev() {
                        self.stack[(dst_start + i) as usize] = Value::default();
                    }

                    self.sp = new_fp + r_cnt - 1;
                    self.fp = new_fp;
                    self.cp += 1;
                    let ret_addr = self.ip + 8;
                    if trace {
                        eprintln!(
                            "[TRACE]   CALL {} pCnt={} rCnt={} newFP={} pushRet={}",
                            functions[func_idx].name, p_cnt, r_cnt, new_fp,
                            ret_addr
                        );
                    }
                    // 固定大小调用栈：cp 即索引位，直接覆盖（对齐 C++ cs[++_cp]=...）
                    if self.call_stack.len() <= self.cp as usize {
                        self.call_stack.resize(self.cp as usize + 1, 0);
                    }
                    self.call_stack[self.cp as usize] = ret_addr;
                    self.ip = functions[func_idx].code_offset;
                }

                rab::OP_RET => {
                    let ret_val = self.reg(0);
                    if trace {
                        eprintln!(
                            "[TRACE]   RET val={} popRet={}",
                            ret_val.data,
                            self.call_stack[self.cp as usize]
                        );
                    }
                    self.sp = self.fp - 1;
                    self.fsp -= 1;
                    self.frame_stack.pop();
                    self.fp = if self.fsp >= 0 {
                        self.frame_stack[self.fsp as usize]
                    } else {
                        0
                    };
                    self.stack[self.fp as usize] = ret_val;
                    self.ip = self.call_stack[self.cp as usize];
                    self.cp -= 1;
                }

                rab::OP_PRINT => {
                    let rs1 = code[ip + 2] as i32;
                    let v = self.reg(rs1);
                    match v.ty {
                        ValueType::Integer => print!("{}", v.data),
                        ValueType::String => print!("{}", self.str_pool[v.data as usize]),
                        ValueType::Array => print!("(数组)"),
                    }
                    println!();
                    self.ip += 8;
                }

                rab::OP_ARRNEW => {
                    let rd = code[ip + 1] as i32;
                    let rs_size = code[ip + 2] as i32;
                    let rs_init = code[ip + 3] as i32;
                    let size = self.reg(rs_size).data;
                    if size < 1 {
                        return Err(VmError::new("数组长度必须为正数"));
                    }
                    let init = self.reg(rs_init);
                    self.array_pool.push(vec![init; size as usize]);
                    let idx = (self.array_pool.len() - 1) as i32;
                    *self.reg_mut(rd) = Value { ty: ValueType::Array, data: idx };
                    self.ip += 8;
                }

                rab::OP_ARRGET => {
                    let rd = code[ip + 1] as i32;
                    let rs_arr = code[ip + 2] as i32;
                    let rs_idx = code[ip + 3] as i32;
                    let h = self.reg(rs_arr);
                    if h.ty != ValueType::Array {
                        return Err(VmError::new("索引的目标不是数组"));
                    }
                    let idx = self.reg(rs_idx).data;
                    let arr = &self.array_pool[h.data as usize];
                    if idx < 0 || idx as usize >= arr.len() {
                        return Err(VmError::new("数组下标越界"));
                    }
                    *self.reg_mut(rd) = arr[idx as usize];
                    self.ip += 8;
                }

                rab::OP_ARRSET => {
                    let rs_val = code[ip + 1] as i32;
                    let rs_arr = code[ip + 2] as i32;
                    let rs_idx = code[ip + 3] as i32;
                    let h = self.reg(rs_arr);
                    if h.ty != ValueType::Array {
                        return Err(VmError::new("索引的目标不是数组"));
                    }
                    let idx = self.reg(rs_idx).data;
                    let arr_len = self.array_pool[h.data as usize].len();
                    if idx < 0 || idx as usize >= arr_len {
                        return Err(VmError::new("数组下标越界"));
                    }
                    let new_val = self.reg(rs_val);
                    self.array_pool[h.data as usize][idx as usize] = new_val;
                    self.ip += 8;
                }

                _ => {
                    return Err(VmError::new(format!(
                        "未知操作码: 0x{:02X} @ ip={}",
                        op, ip
                    )));
                }
            }
        }
        Ok(())
    }
}
