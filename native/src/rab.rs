//! `.rab` 二进制字节码格式解析器
//!
//! 格式契约见仓库根目录 docs/rab-format.md。
//! 所有多字节整数均为小端序。

use std::fmt;

pub const MAGIC: [u8; 4] = [b'R', b'U', b'A', 0];
pub const VERSION: i32 = 1;

/// 指令操作码（与 C++ `includes/Bytecode.h` 的 `Opcode` 枚举一致）
pub const OP_HALT: u8 = 0x00;
pub const OP_MOVI: u8 = 0x01;
pub const OP_MOVS: u8 = 0x02;
pub const OP_MOV: u8 = 0x03;
pub const OP_ADD: u8 = 0x04;
pub const OP_SUB: u8 = 0x05;
pub const OP_MUL: u8 = 0x06;
pub const OP_DIV: u8 = 0x07;
pub const OP_MOD: u8 = 0x08;
pub const OP_EQ: u8 = 0x09;
pub const OP_NE: u8 = 0x0A;
pub const OP_LT: u8 = 0x0B;
pub const OP_GT: u8 = 0x0C;
pub const OP_JMP: u8 = 0x0D;
pub const OP_JIF: u8 = 0x0E;
pub const OP_PUSH: u8 = 0x0F;
pub const OP_CALL: u8 = 0x10;
pub const OP_RET: u8 = 0x11;
pub const OP_PRINT: u8 = 0x12;
pub const OP_LE: u8 = 0x13;
pub const OP_GE: u8 = 0x14;
pub const OP_ARRNEW: u8 = 0x15;
pub const OP_ARRGET: u8 = 0x16;
pub const OP_ARRSET: u8 = 0x17;

#[derive(Debug, Clone)]
pub struct FunctionInfo {
    pub name: String,
    pub param_count: i32,
    pub local_count: i32,
    pub reg_count: i32,
    pub code_offset: i32,
}

#[derive(Debug, Clone)]
pub struct Program {
    pub constants: Vec<i32>,
    pub strings: Vec<String>,
    pub functions: Vec<FunctionInfo>,
    pub entry_point: String,
    pub code: Vec<u8>,
}

#[derive(Debug)]
pub struct RabError {
    pub message: String,
}

impl RabError {
    fn new(msg: impl Into<String>) -> Self {
        RabError { message: msg.into() }
    }
}

impl fmt::Display for RabError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.message)
    }
}

/// 字节游标读取器
struct Reader<'a> {
    data: &'a [u8],
    pos: usize,
}

impl<'a> Reader<'a> {
    fn new(data: &'a [u8]) -> Self {
        Reader { data, pos: 0 }
    }

    fn remaining(&self) -> usize {
        self.data.len().saturating_sub(self.pos)
    }

    fn read_bytes(&mut self, n: usize) -> Result<&'a [u8], RabError> {
        if self.remaining() < n {
            return Err(RabError::new("文件过早结束"));
        }
        let slice = &self.data[self.pos..self.pos + n];
        self.pos += n;
        Ok(slice)
    }

    /// 小端 i32
    fn read_i32(&mut self) -> Result<i32, RabError> {
        let b = self.read_bytes(4)?;
        Ok(i32::from_le_bytes([b[0], b[1], b[2], b[3]]))
    }

    /// 小端 i32，带负数/长度合法性校验（用于长度字段）
    fn read_len(&mut self) -> Result<usize, RabError> {
        let v = self.read_i32()?;
        if v < 0 {
            return Err(RabError::new("非法负长度字段"));
        }
        Ok(v as usize)
    }

    fn read_string(&mut self) -> Result<String, RabError> {
        let len = self.read_len()?;
        if self.remaining() < len {
            return Err(RabError::new("字符串数据截断"));
        }
        let bytes = self.read_bytes(len)?;
        String::from_utf8(bytes.to_vec())
            .map_err(|_| RabError::new("字符串不是合法 UTF-8"))
    }
}

/// 解析 `.rab` 二进制内容
pub fn parse(data: &[u8]) -> Result<Program, RabError> {
    let mut r = Reader::new(data);

    // magic
    if r.remaining() < 8 {
        return Err(RabError::new("不是有效的 .rab 文件（长度不足）"));
    }
    let magic = r.read_bytes(4)?;
    if magic != MAGIC {
        return Err(RabError::new("不是有效的 .rab 文件（magic 不匹配）"));
    }

    // version
    let version = r.read_i32()?;
    if version != VERSION {
        return Err(RabError::new(format!(
            "不支持的 .rab 版本: {}",
            version
        )));
    }

    // 整数常量表
    let c_count = r.read_len()?;
    let mut constants = Vec::with_capacity(c_count);
    for _ in 0..c_count {
        constants.push(r.read_i32()?);
    }

    // 字符串表
    let s_count = r.read_len()?;
    let mut strings = Vec::with_capacity(s_count);
    for _ in 0..s_count {
        strings.push(r.read_string()?);
    }

    // 函数表
    let f_count = r.read_len()?;
    let mut functions = Vec::with_capacity(f_count);
    for _ in 0..f_count {
        let name = r.read_string()?;
        let param_count = r.read_i32()?;
        let local_count = r.read_i32()?;
        let reg_count = r.read_i32()?;
        let code_offset = r.read_i32()?;
        functions.push(FunctionInfo {
            name,
            param_count,
            local_count,
            reg_count,
            code_offset,
        });
    }

    // 入口函数名
    let entry_point = r.read_string()?;

    // 指令流
    let code_size = r.read_len()?;
    if r.remaining() < code_size {
        return Err(RabError::new("指令流数据截断"));
    }
    let code = r.read_bytes(code_size)?.to_vec();

    Ok(Program {
        constants,
        strings,
        functions,
        entry_point,
        code,
    })
}

/// 读取文件并解析
pub fn load(path: &str) -> Result<Program, RabError> {
    let data = std::fs::read(path)
        .map_err(|e| RabError::new(format!("无法打开文件 '{}': {}", path, e)))?;
    parse(&data)
}
