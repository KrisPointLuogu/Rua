// src/parser.rs — 语法分析
//
// 对应 C 版 includes/parser.h。递归下降语法分析器：把 Token 流编译成 AST。
//   - 表达式用「优先级爬升」统一处理二元运算符（parse_expr_prec，
//     替代 C 版原先后缀）;
//   - 语句按关键字分发：变量/数组/如果/当/返回/表达式语句；
//   - 顶层另处理函数定义（喵 ...），函数体与语句互相递归（parse_block ↔
//     parse_statement）。
//
// 与 C 版差异：C 版用指针 + 出参收集动态数组；Rust 版直接用 Vec 返回，
// 错误通过 Result<RuaError> 传播（C 版 error_at 直接退出进程）。

use crate::ast::{Node, Result};
use crate::lexer::{Token, TokenKind};
use crate::rua_err;

/**
 * 语法分析器的工作状态。
 *
 * @param toks Token 数组（来自词法分析器，含结尾 Eof）
 * @param pos  当前读到的 Token 下标
 */
pub struct Parser {
    toks: Vec<Token>,
    pos: usize,
}

impl Parser {
    /// 构造分析器，pos 从 0 开始。
    fn new(toks: Vec<Token>) -> Parser {
        Parser { toks, pos: 0 }
    }

    /**
     * 查看当前位置的 Token 类型（不前进）。
     *
     * 对应 C 版 peek()。
     *
     * @return 当前 Token 的类型
     */
    fn peek(&self) -> TokenKind {
        self.toks[self.pos].kind
    }

    /**
     * 取走当前位置的 Token 并前进。
     *
     * 对应 C 版 advance()（返回 Token 的拷贝）。
     *
     * @return 被取走的 Token
     */
    fn advance(&mut self) -> Token {
        let t = self.toks[self.pos].clone();
        self.pos += 1;
        t
    }

    /**
     * 期望下一个 Token 是指定类型，否则报语法错误。
     *
     * 对应 C 版 expect()。
     *
     * @param k 期望的 Token 类型
     * @return 被取走的 Token
     */
    fn expect(&mut self, k: TokenKind) -> Result<Token> {
        if self.peek() != k {
            return Err(rua_err!(
                self.toks[self.pos].line,
                "语法错误：期望的符号不匹配"
            ));
        }
        Ok(self.advance())
    }

    /**
     * 期望下一个 Token 是标识符，并返回其名字。
     *
     * 对应 C 版 expect_ident()（C 版返回 malloc 副本，Rust 版返回 String）。
     *
     * @return 标识符名字
     */
    fn expect_ident(&mut self) -> Result<String> {
        if self.peek() != TokenKind::Ident {
            return Err(rua_err!(self.toks[self.pos].line, "语法错误：期望标识符"));
        }
        let t = self.advance();
        Ok(t.text.unwrap_or_default())
    }

    /**
     * 解析调用实参：`( 表达式, 表达式, ... )`。
     *
     * 对应 C 版 parse_call_args()。
     *
     * @return 实参节点列表
     */
    fn parse_call_args(&mut self) -> Result<Vec<Node>> {
        self.expect(TokenKind::LP)?;
        let mut args = Vec::new();
        if self.peek() != TokenKind::RP {
            args.push(self.parse_expr()?);
            while self.peek() == TokenKind::Comma {
                self.advance();
                args.push(self.parse_expr()?);
            }
        }
        self.expect(TokenKind::RP)?;
        Ok(args)
    }

    /**
     * 解析表达式（优先级爬升，左结合）。
     *
     * 先解析一个原子，再循环查看下一个运算符：若其优先级不低于
     * min_prec 就取走它并递归解析右操作数（右操作数优先级 +1 保证左结合）。
     * 对应 C 版 parse_expr_prec()。
     *
     * @param min_prec 允许的最低运算符优先级
     * @return 二元表达式对应的 AST 节点
     */
    fn parse_expr_prec(&mut self, min_prec: i32) -> Result<Node> {
        let mut left = self.parse_primary()?;
        loop {
            let op = self.peek();
            let prec = op_prec(op);
            if prec < min_prec {
                break;
            }
            let tok = self.advance();
            let right = self.parse_expr_prec(prec + 1)?;
            left = Node::Binary {
                line: tok.line,
                op,
                left: Box::new(left),
                right: Box::new(right),
            };
        }
        Ok(left)
    }

    /**
     * 解析原子表达式（最高优先级）。
     *
     * 可解析：数字、字符串、括号表达式、标识符、喵叫调用；
     * 之后循环处理后缀：函数调用（标识符后跟 `(` 实参 `)`）与下标（后跟
     * `[` 下标 `]`）。对应 C 版 parse_primary()。
     *
     * @return 原子表达式对应的 AST 节点
     */
    fn parse_primary(&mut self) -> Result<Node> {
        let k = self.peek();
        let mut n: Node;

        if k == TokenKind::Num {
            let t = self.advance();
            n = Node::Num {
                line: t.line,
                num: t.num,
            };
        } else if k == TokenKind::Str {
            let t = self.advance();
            n = Node::Str {
                line: t.line,
                text: t.text.unwrap_or_default(),
            };
        } else if k == TokenKind::LP {
            self.advance();
            n = self.parse_expr()?;
            self.expect(TokenKind::RP)?;
        } else if k == TokenKind::Ident {
            let t = self.advance();
            n = Node::Ident {
                line: t.line,
                name: t.text.unwrap_or_default(),
            };
        } else if k == TokenKind::Miaojiao {
            // 内置打印「喵叫」按函数调用处理。
            let t = self.advance();
            let args = self.parse_call_args()?;
            return Ok(Node::Call {
                line: t.line,
                name: "喵叫".to_string(),
                args,
            });
        } else {
            return Err(rua_err!(self.toks[self.pos].line, "语法错误：期望表达式"));
        }

        // 后缀循环：函数调用（仅 N_IDENT 可调用）与数组下标。
        loop {
            if self.peek() == TokenKind::LP {
                let line = n.line();
                if n.kind() != crate::ast::NodeKind::Ident {
                    return Err(rua_err!(line, "语法错误：只能调用函数"));
                }
                let name = match &n {
                    Node::Ident { name, .. } => name.clone(),
                    _ => unreachable!(),
                };
                let args = self.parse_call_args()?;
                n = Node::Call { line, name, args };
            } else if self.peek() == TokenKind::LB {
                let t = self.advance();
                let index = self.parse_expr()?;
                self.expect(TokenKind::RB)?;
                n = Node::Index {
                    line: t.line,
                    base: Box::new(n),
                    index: Box::new(index),
                };
            } else {
                break;
            }
        }
        Ok(n)
    }

    /**
     * 解析表达式（最低优先级，含赋值）。
     *
     * 先做二元运算优先级爬升，若后面紧跟 '=' 则构造成赋值节点
     * （目标为变量或数组下标）。对应 C 版 parse_expr()。
     *
     * @return 表达式对应的 AST 节点
     */
    fn parse_expr(&mut self) -> Result<Node> {
        let mut n = self.parse_expr_prec(1)?;
        if self.peek() == TokenKind::Assign {
            let t = self.advance();
            let right = self.parse_expr()?;
            n = Node::Assign {
                line: t.line,
                target: Box::new(n),
                right: Box::new(right),
            };
        }
        Ok(n)
    }

    /**
     * 解析变量声明：`变量 名 = 表达式`。
     *
     * 对应 C 版 parse_var()。
     *
     * @return N_VAR 节点
     */
    fn parse_var(&mut self) -> Result<Node> {
        let t = self.advance();
        let name = self.expect_ident()?;
        self.expect(TokenKind::Assign)?;
        let right = self.parse_expr()?;
        Ok(Node::Var {
            line: t.line,
            name,
            right: Box::new(right),
        })
    }

    /**
     * 解析数组声明：`数组 名[长度] = { 广播初始值 }`。
     *
     * 长度必须是数字字面量；{} 里的单个表达式会广播填充整个数组。
     * 对应 C 版 parse_array()。
     *
     * @return N_ARRAY 节点
     */
    fn parse_array(&mut self) -> Result<Node> {
        let t = self.advance();
        let name = self.expect_ident()?;
        self.expect(TokenKind::LB)?;
        if self.peek() != TokenKind::Num {
            return Err(rua_err!(self.toks[self.pos].line, "数组长度必须是数字"));
        }
        let len_t = self.advance();
        self.expect(TokenKind::RB)?;
        self.expect(TokenKind::Assign)?;
        self.expect(TokenKind::LC)?;
        let right = self.parse_expr()?;
        self.expect(TokenKind::RC)?;
        Ok(Node::Array {
            line: t.line,
            name,
            len: len_t.num,
            right: Box::new(right),
        })
    }

    /**
     * 解析如果语句：`如果 条件 那么? { } 否则? { }`。
     *
     * 那么为可选引导词；否则分支也可选。
     * 对应 C 版 parse_if()。
     *
     * @return N_IF 节点
     */
    fn parse_if(&mut self) -> Result<Node> {
        let t = self.advance();
        let cond = self.parse_expr()?;
        if self.peek() == TokenKind::Name {
            self.advance();
        }
        let then = self.parse_block()?;
        let else_b = if self.peek() == TokenKind::Fouze {
            self.advance();
            Some(self.parse_block()?)
        } else {
            None
        };
        Ok(Node::If {
            line: t.line,
            cond: Box::new(cond),
            then: Box::new(then),
            else_b: else_b.map(Box::new),
        })
    }

    /**
     * 解析当循环：`当 条件 那么? { }`。
     *
     * 对应 C 版 parse_while()。
     *
     * @return N_WHILE 节点
     */
    fn parse_while(&mut self) -> Result<Node> {
        let t = self.advance();
        let cond = self.parse_expr()?;
        if self.peek() == TokenKind::Name {
            self.advance();
        }
        let body = self.parse_block()?;
        Ok(Node::While {
            line: t.line,
            cond: Box::new(cond),
            body: Box::new(body),
        })
    }

    /**
     * 解析返回语句：`返回 表达式?`。
     *
     * 后面紧跟 } 或文件结束时视为无返回值（返回默认值 0）。
     * 对应 C 版 parse_return()。
     *
     * @return N_RETURN 节点
     */
    fn parse_return(&mut self) -> Result<Node> {
        let t = self.advance();
        let value = if self.peek() != TokenKind::LC
            && self.peek() != TokenKind::Eof
            && self.peek() != TokenKind::RC
        {
            Some(Box::new(self.parse_expr()?))
        } else {
            None
        };
        Ok(Node::Return {
            line: t.line,
            value,
        })
    }

    /**
     * 解析单条语句。
     *
     * 按关键字分发到对应的语句解析函数；
     * 其余一律按表达式语句处理（赋值、函数调用等）。
     * 对应 C 版 parse_statement()。
     *
     * @return 语句对应的 AST 节点
     */
    fn parse_statement(&mut self) -> Result<Node> {
        match self.peek() {
            TokenKind::Bian => self.parse_var(),
            TokenKind::Shizu => self.parse_array(),
            TokenKind::Ruguo => self.parse_if(),
            TokenKind::Dang => self.parse_while(),
            TokenKind::Fanhui => self.parse_return(),
            _ => self.parse_expr(),
        }
    }

    /**
     * 解析代码块：`{ 语句* }`。
     *
     * 与 parse_statement 互相递归。对应 C 版 parse_block()。
     *
     * @return N_BLOCK 节点，包含语句列表
     */
    fn parse_block(&mut self) -> Result<Node> {
        let t = self.expect(TokenKind::LC)?;
        let mut stmts = Vec::new();
        while self.peek() != TokenKind::RC && self.peek() != TokenKind::Eof {
            stmts.push(self.parse_statement()?);
        }
        self.expect(TokenKind::RC)?;
        Ok(Node::Block {
            line: t.line,
            stmts,
        })
    }

    /**
     * 解析函数定义：`喵 函数名(形参*) { 函数体 }`。
     *
     * 对应 C 版 parse_function()。
     *
     * @return N_FUNC 节点，随后由 compile 阶段注册进函数表
     */
    fn parse_function(&mut self) -> Result<Node> {
        let t = self.expect(TokenKind::Miao)?;
        let name = self.expect_ident()?;
        self.expect(TokenKind::LP)?;
        let mut params = Vec::new();
        if self.peek() != TokenKind::RP {
            params.push(self.expect_ident()?);
            while self.peek() == TokenKind::Comma {
                self.advance();
                params.push(self.expect_ident()?);
            }
        }
        self.expect(TokenKind::RP)?;
        let body = self.parse_block()?;
        Ok(Node::Func {
            line: t.line,
            name,
            params,
            body: Box::new(body),
        })
    }

    /**
     * 解析 Token 流，产出程序节点与顶层函数表。
     *
     * 顶层语句存入程序节点；顶层函数定义（喵 ...）单独收集（编译为
     * Function 并注册进运行时函数表）。对应 C 版 compile() 的循环部分。
     *
     * @param toks 词法分析器产出的 Token 数组（含结尾 Eof）
     * @return (N_PROG 程序节点, 顶层函数定义列表)
     */
    pub fn parse_tokens(toks: Vec<Token>) -> Result<(Node, Vec<Node>)> {
        let mut ps = Parser::new(toks);
        let mut stmts = Vec::new();
        let mut fns = Vec::new();
        while ps.peek() != TokenKind::Eof {
            if ps.peek() == TokenKind::Miao {
                fns.push(ps.parse_function()?);
            } else {
                stmts.push(ps.parse_statement()?);
            }
        }
        Ok((Node::Prog { stmts }, fns))
    }
}

/**
 * 返回二元运算符的优先级（数字越大越优先），非运算符返回 0。
 *
 * 对应 C 版 op_prec()。
 *
 * @param k Token 类型
 * @return 优先级等级（1~4），不是二元运算符则为 0
 */
fn op_prec(k: TokenKind) -> i32 {
    match k {
        TokenKind::Eqeq | TokenKind::Neq => 1,
        TokenKind::Gt | TokenKind::Lt | TokenKind::Ge | TokenKind::Le => 2,
        TokenKind::Plus | TokenKind::Minus => 3,
        TokenKind::Star | TokenKind::Slash | TokenKind::Percent => 4,
        _ => 0,
    }
}
