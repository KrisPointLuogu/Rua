# 语法分析是怎么生成的？

## 输入

Token 列表（来自词法分析）。

## 输出

`Program` — AST（抽象语法树）根节点，内含函数列表和顶层语句列表。

## 算法

递归下降解析器（`Parser` 类），每个文法规则对应一个 `parseXxx()` 方法：

```
program       = (function | statement)*
function      = "喵" IDENTIFIER "(" [params] ")" block
block         = "{" statement* "}"
statement     = varDecl | ifStmt | whileStmt | returnStmt | exprStmt
expression    = assignment (优先级从低到高)
```

## 流程

1. **`parseProgram()`** — 循环消费 token，遇到 `喵` 则 `parseFunction()`，否则 `parseStatement()` 作为顶层语句

2. **`parseFunction()`** — 读函数名、参数列表、body block，返回 `Function` 节点

3. **`parseStatement()`** — 按当前 token 类型分发：
   - `变量` → `parseVarDecl()`
   - `如果` → `parseIfStmt()`
   - `当` → `parseWhileStmt()`
   - `返回` → `parseReturnStmt()`
   - 其他 → `parseExpression()` 作为表达式语句

4. **表达式解析** — 优先级爬升：
   ```
   parseExpression  → parseAssignment     (=)
   parseAssignment  → parseEquality        (== !=)
   parseEquality   → parseComparison       (> < >= <=)
   parseComparison → parseAddition         (+ -)
   parseAddition   → parseMultiplication   (* / %)
   parseMultiplication → parsePrimary      (字面量/标识符/括号/函数调用)
   ```

5. **错误处理** — 遇到非法 token 时抛出 `ParserError`，终止解析（无错误恢复）

## 关键代码

`src/语法分析器.cpp` — `Parser` 类，约 550 行
`includes/语法树.h` — AST 节点定义（`Program`, `Function`, `IfStmt`, `BinaryExpr` 等）
