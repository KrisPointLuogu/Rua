// src/lexer.rs — 词法分析
//
// 对应 C 版 includes/lexer.h（includes/utf8.h 的 UTF-8 职责由 Rust 的
// `char` 码点迭代承担，无需单独模块）。
//
// 把源码字符串切成一个个 Token（词法单元）：
//   - 识别中文关键字（喵、变量、如果、那么、否则、当、返回、喵叫、数组）；
//   - 识别标识符、整数、字符串字面量；
//   - 识别运算符与界符；
//   - 跳过空白与注释（# 单行、#* ... *# 多行）。
//
// 与 C 版的差异：C 用「字节指针 + utf8_peek/utf8_advance」手工推进；
// Rust 版把源码一次性收集为 `Vec<char>`（每个元素一个 Unicode 码点），
// 词法分析器用 pos 下标推进，语义与 C 版完全一致。

use crate::ast::Result;
use crate::rua_err;

/**
 * Token 类型。
 *
 * TK_ 前缀 + 英文缩写表示中文关键字：
 *   TK_MIAO=喵 TK_BIAN=变量
 *   TK_RUGUO=如果 TK_NAME=那么
 *   TK_FOUZE=否则
 *   TK_DANG=当 TK_FANHUI=返回
 *   TK_MIAOJIAO=喵叫 TK_SHIZU=数组
 * 其余为数字、字符串、标识符、运算符和界符。
 */
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum TokenKind {
    /// 文件结束（TK_EOF）。
    Eof,
    /// 数字字面量（TK_NUM）。
    Num,
    /// 字符串字面量（TK_STR）。
    Str,
    /// 标识符（TK_IDENT）。
    Ident,
    /// 喵（函数定义，TK_MIAO）。
    Miao,
    /// 变量（TK_BIAN）。
    Bian,
    /// 如果（TK_RUGUO）。
    Ruguo,
    /// 那么（TK_NAME，可选引导词）。
    Name,
    /// 否则（TK_FOUZE）。
    Fouze,
    /// 当（TK_DANG）。
    Dang,
    /// 返回（TK_FANHUI）。
    Fanhui,
    /// 喵叫（TK_MIAOJIAO，内置打印）。
    Miaojiao,
    /// 数组（TK_SHIZU）。
    Shizu,
    /// 左括号（
    LP,
    /// 右括号）
    RP,
    /// 左中括号[
    LB,
    /// 右中括号]
    RB,
    /// 左花括号{
    LC,
    /// 右花括号}
    RC,
    /// 逗号,
    Comma,
    /// 加号+
    Plus,
    /// 减号-
    Minus,
    /// 乘号*
    Star,
    /// 除号/
    Slash,
    /// 模号%
    Percent,
    /// 等于==
    Eqeq,
    /// 不等于!=
    Neq,
    /// 大于>
    Gt,
    /// 小于<
    Lt,
    /// 大于等于>=
    Ge,
    /// 小于等于<=
    Le,
    /// 赋值=
    Assign,
}

/**
 * 一个词法单元。
 *
 * @param kind token 类型
 * @param text 附加文本：标识符名 / 字符串字面量内容（数字与运算符为 None）
 * @param num  数字字面量的数值（仅 kind == Num 时有效）
 * @param line 该 token 所在源文件行号（用于报错定位）
 */
#[derive(Debug, Clone)]
pub struct Token {
    pub kind: TokenKind,
    pub text: Option<String>,
    pub num: i64,
    pub line: usize,
}

impl Token {
    /**
     * 构造一个最普通的 Token（不含文本）。
     *
     * text 统一置为 None，仅数字 token 需要额外设置 num。
     * 对应 C 版 token_make()。
     *
     * @param kind token 类型
     * @param num  数字字面量的值（非数字时填 0）
     * @param line 所在行号
     * @return 构造好的 Token
     */
    pub fn new(kind: TokenKind, num: i64, line: usize) -> Token {
        Token {
            kind,
            text: None,
            num,
            line,
        }
    }
}

/**
 * 词法分析器的工作状态。
 *
 * C 版用字节指针 p + line；Rust 版用码点数组 + 下标，语义一致。
 *
 * @param chars 源码的 Unicode 码点序列
 * @param pos   指向下一个待扫描码点的下标
 * @param line  当前所在行号（遇换行 +1，用于报错定位）
 */
struct Lexer {
    chars: Vec<char>,
    pos: usize,
    line: usize,
}

/**
 * 判断某个字符是否可以用在标识符中。
 *
 * 标识符字符包括：英文字母（大小写）、数字、下划线，以及常用中文字符
 * （Unicode 范围 U+4E00 ~ U+9FA5）。词法分析器用本函数切分标识符/关键字。
 *
 * @param c 要判断的 Unicode 码点
 * @return true 表示可作为标识符字符
 */
fn is_ident_char(c: char) -> bool {
    ('a'..='z').contains(&c)
        || ('A'..='Z').contains(&c)
        || ('0'..='9').contains(&c)
        || c == '_'
        || (0x4E00..=0x9FA5).contains(&(c as u32))
}

/**
 * 判断一段 UTF-8 文本是不是关键字。
 *
 * 把词法分析器切出的完整标识符串与关键字表逐项比对；
 * 命中返回对应关键字类型，否则返回 Ident（普通标识符）。
 *
 * @param s 标识符文本
 * @return 对应的关键字 TokenKind，或 Ident
 */
fn keyword_kind(s: &str) -> TokenKind {
    match s {
        "喵" => TokenKind::Miao,
        "变量" => TokenKind::Bian,
        "如果" => TokenKind::Ruguo,
        "那么" => TokenKind::Name,
        "否则" => TokenKind::Fouze,
        "当" => TokenKind::Dang,
        "返回" => TokenKind::Fanhui,
        "喵叫" => TokenKind::Miaojiao,
        "数组" => TokenKind::Shizu,
        _ => TokenKind::Ident,
    }
}

impl Lexer {
    /// 构造词法分析器：把源码收集为码点数组。
    fn new(src: &str) -> Lexer {
        Lexer {
            chars: src.chars().collect(),
            pos: 0,
            line: 1,
        }
    }

    /// 查看当前位置的码点（不解码前进）；越界返回 '\0'。
    fn peek(&self) -> char {
        if self.pos >= self.chars.len() {
            '\0'
        } else {
            self.chars[self.pos]
        }
    }

    /// 前进一个码点(utf8_advance)。
    fn advance(&mut self) {
        if self.pos < self.chars.len() {
            self.pos += 1;
        }
    }

    /**
     * 扫描一个字符串字面量，处理转义序列。
     *
     * 支持成对引号："" ''（英文）与 “” ‘’（中文全角）。
     * 字符串内可包含 \n \t \r \\ \" \' 转义（仅英文引号内生效）。
     * 字符串内容存入 Token.text（对应 C 版 lex_string）。
     *
     * @param q    起始引号字符（决定对应的结束引号）
     * @param line 字符串起始所在行号
     * @return 内容为字符串字面量的 Token
     */
    fn lex_string(&mut self, q: char, line: usize) -> Result<Token> {
        let close = match q {
            '"' => '"',
            '\'' => '\'',
            '\u{201C}' => '\u{201D}', // “ → ”
            _ => '\u{2019}',          // ‘ → ’
        };
        self.advance();

        let mut buf = String::new();
        loop {
            if self.peek() == '\0' {
                return Err(rua_err!(line, "字符串未闭合"));
            }
            let c = self.peek();
            if c == '\n' {
                self.line += 1;
            }
            if c == close {
                self.advance();
                break;
            }

            // 转义仅英文引号内生效；未知转义序列直接丢弃（与 C 版一致）。
            if c == '\\' && (q == '"' || q == '\'') {
                self.advance();
                let e = self.peek();
                let esc = match e {
                    'n' => Some('\n'),
                    't' => Some('\t'),
                    'r' => Some('\r'),
                    '\\' => Some('\\'),
                    '"' => Some('"'),
                    '\'' => Some('\''),
                    _ => None,
                };
                self.advance();
                if let Some(ch) = esc {
                    buf.push(ch);
                }
                continue;
            }

            buf.push(c);
            self.advance();
        }

        let mut t = Token::new(TokenKind::Str, 0, line);
        t.text = Some(buf);
        Ok(t)
    }

    /**
     * 扫描下一个 Token（跳过空白与注释）。
     *
     * 依次识别：注释 -> 数字 -> 标识符/关键字 -> 字符串 -> 运算符/界符。
     * 遇到未知字符或未闭合字符串会返回错误（对应 C 版 lex_next + error_at）。
     *
     * @return 扫描出的下一个 Token；源码扫完时返回 Eof
     */
    fn lex_next(&mut self) -> Result<Token> {
        loop {
            if self.peek() == '\0' {
                return Ok(Token::new(TokenKind::Eof, 0, self.line));
            }
            let c0 = self.peek();

            // 换行：推进并计数行号。
            if c0 == '\n' {
                self.advance();
                self.line += 1;
                continue;
            }
            // 空白：空格、制表符、回车。
            if c0 == ' ' || c0 == '\t' || c0 == '\r' {
                self.advance();
                continue;
            }

            // 注释：# 单行，或 #* ... *# 多行（不嵌套）。
            if c0 == '#' {
                self.advance();
                if self.peek() == '*' {
                    self.advance();
                    loop {
                        if self.peek() == '\0' {
                            break;
                        }
                        if self.peek() == '*' && self.chars.get(self.pos + 1) == Some(&'#') {
                            self.advance();
                            self.advance();
                            break;
                        }
                        if self.peek() == '\n' {
                            self.line += 1;
                        }
                        self.advance();
                    }
                } else {
                    while self.peek() != '\0' && self.peek() != '\n' {
                        self.advance();
                    }
                }
                continue;
            }

            // 数字：连续的 ASCII 十进制数字（与 C 版一致，仅 0-9）。
            if c0.is_ascii_digit() {
                let mut n: i64 = 0;
                while self.peek().is_ascii_digit() {
                    n = n * 10 + (self.peek() as i64 - '0' as i64);
                    self.advance();
                }
                return Ok(Token::new(TokenKind::Num, n, self.line));
            }

            // 标识符 / 关键字：连续 is_ident_char 字符。
            if is_ident_char(self.peek()) {
                let mut text = String::new();
                while is_ident_char(self.peek()) {
                    text.push(self.peek());
                    self.advance();
                }
                let k = keyword_kind(&text);
                if k == TokenKind::Ident {
                    let mut t = Token::new(TokenKind::Ident, 0, self.line);
                    t.text = Some(text);
                    return Ok(t);
                }
                return Ok(Token::new(k, 0, self.line));
            }

            // 字符串：英文引号与中文全角引号。
            if c0 == '"' || c0 == '\'' {
                return self.lex_string(c0, self.line);
            }
            let c = self.peek();
            if c == '\u{201C}' || c == '\u{2018}' {
                return self.lex_string(c, self.line);
            }

            // 运算符与界符。
            let line = self.line;
            match c0 {
                '+' => {
                    self.advance();
                    return Ok(Token::new(TokenKind::Plus, 0, line));
                }
                '-' => {
                    self.advance();
                    return Ok(Token::new(TokenKind::Minus, 0, line));
                }
                '*' => {
                    self.advance();
                    return Ok(Token::new(TokenKind::Star, 0, line));
                }
                '/' => {
                    self.advance();
                    return Ok(Token::new(TokenKind::Slash, 0, line));
                }
                '%' => {
                    self.advance();
                    return Ok(Token::new(TokenKind::Percent, 0, line));
                }
                '(' => {
                    self.advance();
                    return Ok(Token::new(TokenKind::LP, 0, line));
                }
                ')' => {
                    self.advance();
                    return Ok(Token::new(TokenKind::RP, 0, line));
                }
                '[' => {
                    self.advance();
                    return Ok(Token::new(TokenKind::LB, 0, line));
                }
                ']' => {
                    self.advance();
                    return Ok(Token::new(TokenKind::RB, 0, line));
                }
                '{' => {
                    self.advance();
                    return Ok(Token::new(TokenKind::LC, 0, line));
                }
                '}' => {
                    self.advance();
                    return Ok(Token::new(TokenKind::RC, 0, line));
                }
                ',' => {
                    self.advance();
                    return Ok(Token::new(TokenKind::Comma, 0, line));
                }
                '=' => {
                    self.advance();
                    if self.peek() == '=' {
                        self.advance();
                        return Ok(Token::new(TokenKind::Eqeq, 0, line));
                    }
                    return Ok(Token::new(TokenKind::Assign, 0, line));
                }
                '!' => {
                    self.advance();
                    if self.peek() == '=' {
                        self.advance();
                        return Ok(Token::new(TokenKind::Neq, 0, line));
                    }
                    return Err(rua_err!(line, "无法识别的字符"));
                }
                '>' => {
                    self.advance();
                    if self.peek() == '=' {
                        self.advance();
                        return Ok(Token::new(TokenKind::Ge, 0, line));
                    }
                    return Ok(Token::new(TokenKind::Gt, 0, line));
                }
                '<' => {
                    self.advance();
                    if self.peek() == '=' {
                        self.advance();
                        return Ok(Token::new(TokenKind::Le, 0, line));
                    }
                    return Ok(Token::new(TokenKind::Lt, 0, line));
                }
                _ => {
                    return Err(rua_err!(line, "无法识别的字符"));
                }
            }
        }
    }

    /**
     * 对整个源码执行词法分析。
     *
     * 循环调用 lex_next 直到 Eof，把所有 Token 收集进动态数组
     * （含结尾的 Eof）。对应 C 版 lex_all()。
     *
     * @return 全部 Token 的数组（含结尾 Eof）
     */
    fn lex_all(mut self) -> Result<Vec<Token>> {
        let mut toks = Vec::new();
        loop {
            let t = self.lex_next()?;
            let end = t.kind == TokenKind::Eof;
            toks.push(t);
            if end {
                break;
            }
        }
        Ok(toks)
    }
}

/**
 * 对整个源码执行词法分析（公开入口）。
 *
 * @param src 源码字符串（UTF-8）
 * @return 全部 Token 的数组（含结尾 Eof）；词法错误返回 RuaError
 */
pub fn lex_all(src: &str) -> Result<Vec<Token>> {
    Lexer::new(src).lex_all()
}

/**
 * 返回 Token 类型的可读中文名（仅调试输出用）。
 *
 * 对应 C 版 token_kind_name()（DEBUG 宏下启用）。
 *
 * @param k Token 类型
 * @return 中文字符串名
 */
pub fn token_kind_name(k: TokenKind) -> &'static str {
    match k {
        TokenKind::Eof => "文件结束",
        TokenKind::Num => "数字",
        TokenKind::Str => "字符串",
        TokenKind::Ident => "标识符",
        TokenKind::Miao => "喵(函数)",
        TokenKind::Bian => "变量",
        TokenKind::Ruguo => "如果",
        TokenKind::Name => "那么",
        TokenKind::Fouze => "否则",
        TokenKind::Dang => "当",
        TokenKind::Fanhui => "返回",
        TokenKind::Miaojiao => "喵叫",
        TokenKind::Shizu => "数组",
        TokenKind::LP => "左括号(",
        TokenKind::RP => "右括号)",
        TokenKind::LB => "左中括号[",
        TokenKind::RB => "右中括号]",
        TokenKind::LC => "左花括号{",
        TokenKind::RC => "右花括号}",
        TokenKind::Comma => "逗号",
        TokenKind::Plus => "加号+",
        TokenKind::Minus => "减号-",
        TokenKind::Star => "乘号*",
        TokenKind::Slash => "除号/",
        TokenKind::Percent => "模号%",
        TokenKind::Eqeq => "等于==",
        TokenKind::Neq => "不等于!=",
        TokenKind::Gt => "大于>",
        TokenKind::Lt => "小于<",
        TokenKind::Ge => "大于等于>=",
        TokenKind::Le => "小于等于<=",
        TokenKind::Assign => "赋值=",
    }
}
