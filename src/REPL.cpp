#include "REPL.h"
#include <iostream>
#include <sstream>
#include <string>
#include "FileSystem.h"
#include "UTF32Support.h"
#include "Globals.h"
#include "Bytecode.h"
#include "VM.h"
#include "Lexer.h"
#include "SemanticAnalysis.h"
#include "Parser.h"
#include "ColorOutput.h"
#ifdef OPTIMIZATION
#include "IR/CFG.h"
#include "JIT.h"
#include "Optimizer/PassManager.h"
#include "TACGenerator.h"
#endif

#ifdef _DEBUG
#include "ASTDump.h"
#include "DebugOutput.h"
#ifdef OPTIMIZATION
#include "IR/TACDump.h"
#endif
#endif

using std::string;
using std::unique_ptr;
using std::vector;

// ==================== AST 抽象语法树调试输出 ====================

#ifdef _DEBUG
string 运算符符号(int 操作符)
{
    switch (操作符) {
    case 词法分析器类::等号: return "=";
    case 词法分析器类::加号: return "+";
    case 词法分析器类::减号: return "-";
    case 词法分析器类::乘号: return "*";
    case 词法分析器类::除号: return "/";
    case 词法分析器类::等于: return "==";
    case 词法分析器类::不等于: return "!=";
    case 词法分析器类::大于: return ">";
    case 词法分析器类::小于: return "<";
    case 词法分析器类::模: return "%";
    case 词法分析器类::乘方: return "^";
    case 词法分析器类::整除: return "//";
    case 词法分析器类::大于等于: return ">=";
    case 词法分析器类::小于等于: return "<=";
    default: return "?";
    }
}

void 打印AST节点(const ASTNode& 节点, string 前缀, bool 末尾)
{
    string 当前行 = 前缀 + (末尾 ? "└─ " : "├─ ");
    string 子前缀 = 前缀 + (末尾 ? "   " : "│  ");

    switch (节点.getType()) {
    case NodeType::PROGRAM: {
        const auto& n = static_cast<const Program&>(节点);
        调试输出(当前行 + "程序");
        size_t 索引 = 0;
        size_t 总数 = n.functions.size() + n.topLevelStmts.size();
        for (auto& f : n.functions) {
            打印AST节点(*f, 子前缀, 索引 == 总数 - 1);
            ++索引;
        }
        for (auto& s : n.topLevelStmts) {
            打印AST节点(*s, 子前缀, 索引 == 总数 - 1);
            ++索引;
        }
        break;
    }
    case NodeType::FUNCTION: {
        const auto& n = static_cast<const Function&>(节点);
        string 参数 = "";
        for (size_t i = 0; i < n.params.size(); i++) {
            if (i) 参数 += ", ";
            参数 += n.params[i];
        }
        调试输出(当前行 + "函数: " + n.name + " (参数: " + 参数 + ")");
        打印AST节点(*n.body, 子前缀, true);
        break;
    }
    case NodeType::BLOCK: {
        const auto& n = static_cast<const Block&>(节点);
        调试输出(当前行 + "语句块");
        for (size_t i = 0; i < n.statements.size(); i++)
            打印AST节点(*n.statements[i], 子前缀,
                         i == n.statements.size() - 1);
        break;
    }
    case NodeType::VAR_DECL: {
        const auto& n = static_cast<const VarDecl&>(节点);
        调试输出(当前行 + "变量声明: " + n.name);
        打印AST节点(*n.initializer, 子前缀, true);
        break;
    }
    case NodeType::ARRAY_DECL: {
        const auto& n = static_cast<const ArrayDecl&>(节点);
        调试输出(当前行 + "数组声明: " + n.name);
        for (auto& s : n.sizes) {
            调试输出(子前缀 + "维度:");
            打印AST节点(*s, 子前缀 + "   ", true);
        }
        if (n.initialValue)
            打印AST节点(*n.initialValue, 子前缀, true);
        else
            调试输出(子前缀 + "(无初始化，默认全 0)");
        break;
    }
    case NodeType::ARRAY_LITERAL: {
        const auto& n = static_cast<const ArrayLiteral&>(节点);
        调试输出(当前行 + "数组初始化列表");
        for (size_t i = 0; i < n.elements.size(); i++)
            打印AST节点(*n.elements[i], 子前缀,
                         i == n.elements.size() - 1);
        break;
    }
    case NodeType::IF_STMT: {
        const auto& n = static_cast<const IfStmt&>(节点);
        调试输出(当前行 + "如果语句");
        打印AST节点(*n.condition, 子前缀, false);
        打印AST节点(*n.thenBranch, 子前缀, n.elseBranch == nullptr);
        if (n.elseBranch) 打印AST节点(*n.elseBranch, 子前缀, true);
        break;
    }
    case NodeType::WHILE_STMT: {
        const auto& n = static_cast<const WhileStmt&>(节点);
        调试输出(当前行 + "当语句");
        打印AST节点(*n.condition, 子前缀, false);
        打印AST节点(*n.body, 子前缀, true);
        break;
    }
    case NodeType::RETURN_STMT: {
        const auto& n = static_cast<const ReturnStmt&>(节点);
        调试输出(当前行 + "返回语句");
        打印AST节点(*n.value, 子前缀, true);
        break;
    }
    case NodeType::EXPR_STMT: {
        const auto& n = static_cast<const ExprStmt&>(节点);
        调试输出(当前行 + "表达式语句");
        打印AST节点(*n.expression, 子前缀, true);
        break;
    }
    case NodeType::BINARY_EXPR: {
        const auto& n = static_cast<const BinaryExpr&>(节点);
        调试输出(当前行 + "二元运算: " + 运算符符号(n.op));
        打印AST节点(*n.left, 子前缀, false);
        打印AST节点(*n.right, 子前缀, true);
        break;
    }
    case NodeType::CALL_EXPR: {
        const auto& n = static_cast<const CallExpr&>(节点);
        调试输出(当前行 + "函数调用: " + n.callee);
        for (size_t i = 0; i < n.args.size(); i++)
            打印AST节点(*n.args[i], 子前缀, i == n.args.size() - 1);
        break;
    }
    case NodeType::INDEX_EXPR: {
        const auto& n = static_cast<const IndexExpr&>(节点);
        调试输出(当前行 + "索引访问");
        打印AST节点(*n.base, 子前缀, false);
        打印AST节点(*n.index, 子前缀, true);
        break;
    }
    case NodeType::NUMBER_LITERAL: {
        const auto& n = static_cast<const NumberLiteral&>(节点);
        调试输出(当前行 + "数字: " + std::to_string(n.value));
        break;
    }
    case NodeType::STRING_LITERAL: {
        const auto& n = static_cast<const StringLiteral&>(节点);
        调试输出(当前行 + "字符串: \"" + n.value + "\"");
        break;
    }
    case NodeType::IDENTIFIER: {
        const auto& n = static_cast<const Identifier&>(节点);
        调试输出(当前行 + "标识符: " + n.name);
        break;
    }
    }
}

void 打印AST树(const Program& 程序)
{
    调试输出("====== AST 抽象语法树 ======");
    调试输出("程序");
    size_t 索引 = 0;
    size_t 总数 = 程序.functions.size() + 程序.topLevelStmts.size();
    for (auto& f : 程序.functions) {
        打印AST节点(*f, "", 索引 == 总数 - 1);
        ++索引;
    }
    for (auto& s : 程序.topLevelStmts) {
        打印AST节点(*s, "", 索引 == 总数 - 1);
        ++索引;
    }
    调试输出("====== AST 输出结束 ======");
}
#endif

// ==================== 编译并运行一个完整的 Rua 程序 ====================

void 编译并运行(const string& 源码)
{
    try {
        输出文本("【阶段一】词法分析 ...", "青");
        词法分析器类 词法分析器(源码);
        vector<词法分析器类::令牌> 令牌列表 = 词法分析器.分析();

        if (令牌列表.empty()) {
            输出文本("没有 Token 可解析", "YY");
            return;
        }

        输出文本("完成，共 " + std::to_string(令牌列表.size()) + " 个 Token",
                 "GG");

        输出文本("【阶段二】语法分析 ...", "青");
        Parser 解析器(令牌列表);
        auto 程序 = 解析器.parseProgram();
#ifdef _DEBUG
        打印AST树(*程序);
#endif
        输出文本("完成，共 " + std::to_string(程序->functions.size())
                     + " 个函数",
                 "GG");

#ifdef _DEBUG
        调试输出("AST 语法树:", "YY");
        dumpAST(*程序);
#endif

        输出文本("【阶段三】语义分析 ...", "青");
        SemanticAnalyzer 语义分析器;
        语义分析器.analyze(*程序);
        输出文本("通过", "GG");

        输出文本("【阶段四】字节码生成 ...", "青");

#ifdef OPTIMIZATION
        // TAC-based pipeline: AST -> TAC -> optimize -> bytecode
        输出文本("  [优化] AST → TAC ...", "WW");
        TACGenerator tacGen(语义分析器.getSymbolTable());
        TACProgram tac = tacGen.generate(*程序);

#ifdef _DEBUG
        调试输出("优化前 TAC (IR):", "YY");
        dumpTACProgram(tac);
#endif

        输出文本("  [优化] 运行优化 pass ...", "WW");
        PassManager passMgr;
        passMgr.runAll(tac);

#ifdef _DEBUG
        调试输出("优化后 TAC (IR):", "YY");
        dumpTACProgram(tac);
#endif

        输出文本("  [优化] TAC → 字节码 ...", "WW");
        std::vector<int> regCounts;
        for (auto& f : tac.functions) regCounts.push_back(f.regCount);
        BytecodeGenerator 生成器(语义分析器.getSymbolTable());
        BytecodeProgram 字节码 = 生成器.generateFromTAC(tac, regCounts);
#else
        BytecodeGenerator 生成器(语义分析器.getSymbolTable());
        BytecodeProgram 字节码 = 生成器.generate(*程序);
#endif

#ifdef _DEBUG
        字节码.print();
#endif

        输出文本("完成，共 " + std::to_string(字节码.code.size())
                     + " 字节的字节码",
                 "GG");

#if defined(OPTIMIZATION) && (defined(_WIN64) || !defined(_WIN32))
        输出文本("【阶段四·五】JIT 编译 ...", "青");
        {
            JITCompiler jit;
            jit.compile(字节码);
        }
        输出文本("JIT 完成", "GG");
#endif

        输出文本("【阶段五】虚拟机执行 ...", "青");
        VM 虚拟机;
        虚拟机.run(字节码);
        std::cout << std::endl;
        输出文本("程序执行完毕！", "GG");
    } catch (const ParserError& e) {
        输出文本(string("语法错误：") + e.what(), "RR");
    } catch (const SemanticError& e) {
        输出文本(string("语义错误：") + e.what(), "RR");
    } catch (const VMError& e) {
        输出文本(string("运行时错误：") + e.what(), "RR");
    } catch (const std::exception& e) {
        输出文本(string("错误：") + e.what(), "RR");
    }
}

// ==================== 从文件编译运行 ====================

void 运行文件(const string& 路径)
{
    string 源码;
    if (!arch::readFile(路径, 源码)) {
        输出文本("错误：无法打开文件 '" + 路径 + "'", "RR");
        return;
    }

    输出文本("正在编译 " + 路径 + " ...", "青");
    编译并运行(源码);
}

// ==================== 原有 REPL 函数 (保留) ====================

void 基本界面()
{
    输出文本("+——————————————————————————", "青");
    输出文本("Rua—中文编程语言解释器");

#ifdef _DEBUG
    输出文本(
        "当前处于[DEBUG/"
        "调试模式]\n请注意，这将会启用全部DEBUG功能，且存在与[RELEASE]"
        "模式行为不符的可能性",
        "YY", true, "粗体");

    if (全局内容.系统信息.find("Windows") != string::npos
        && 全局内容.系统信息.find("x86_64") != string::npos)
        输出文本("EXE位于 [" + 全局内容.系统信息 + "] 系统编译", "GG");
    else if (全局内容.系统信息.find("Windows") != string::npos
             && 全局内容.系统信息.find("x86") != string::npos)
        输出文本(
            "EXE位于 [" + 全局内容.系统信息
                + "] 系统编译，请注意，程序对此Windows版本可能存在兼容性问题",
            "YY");
    else if (全局内容.系统信息.find("Linux") != string::npos
             || 全局内容.系统信息.find("macOS") != string::npos)
        输出文本("EXE位于 [" + 全局内容.系统信息
                     + "] 系统编译，当前存在不稳定或功能异常风险",
                 "YY");
    else {
        输出文本("EXE位于 [" + 全局内容.系统信息 + "] 系统编译，如不稳定或功能异常风险导致任何形式的后果，作者概不负责", "RR");
        输出文本("[Use with caution / 谨慎使用]", "RR");
    }

#else
    输出文本("[刘小黑]" + 全局内容.用户名 + "泥嚎！");
    输出文本("喵de语录："
             + 全局内容.喵de语录[生成随机数(
                 0, static_cast<int>(全局内容.喵de语录.size()) - 1)]);
#endif

    输出文本("+——————————————————————————", "青");
    输出文本("输入 Rua 代码（多行以 '运行' 结束并执行）：", "BB");
}

string 输入()
{
#ifdef _DEBUG
    输出文本("(DEBUG) ", "BB", false);
#else
    输出文本("(>ω<) ", "BB", false);
#endif
    string 输入;
    std::getline(std::cin, 输入);

    if (std::cin.eof()) { return "EOF"; }

    return 输入;
}

void 退出()
{
    输出文本("[刘小黑]唔...先退出了嗷", "YY");
    exit(0);
}

bool 简单分析行(string& 行)
{
#ifdef _DEBUG
    调试输出("输入的原始字节: ", "WW", false);
    for (unsigned char 字符 : 行) { std::cout << std::hex << (int)字符 << " "; }
    std::cout << std::dec << std::endl;
#endif
    if (行 == "")
        return 0;
    else if (行 == "EOF")
        退出();
    return 1;
}

// ==================== 交互式 REPL ====================

void 交互与执行()
{
    全局内容.用户名 = 获取系统用户名();
    基本界面();

    string 累积源码 = "";

    while (1) {
        string 行;
        行 = 输入();
        if (!简单分析行(行)) continue;

        if (行 == "运行") {
            if (!累积源码.empty()) {
                编译并运行(累积源码);
                累积源码.clear();
                输出文本("输入 Rua 代码（多行以 '运行' 结束并执行）：", "BB");
            }
            continue;
        }

        累积源码 += 行 + "\n";
    }
}
