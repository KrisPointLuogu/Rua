#include "REPL.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include "UTF32支持.h"
#include "全局内容.h"
#include "字节码.h"
#include "虚拟机.h"
#include "词法分析器.h"
#include "语义分析.h"
#include "语法分析器.h"
#include "输出彩色支持.h"
#ifdef OPTIMIZATION
#include "JIT.h"
#endif

#ifdef _DEBUG
#include "调试输出支持.h"
#endif

using std::string;
using std::unique_ptr;
using std::vector;

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
        输出文本("完成，共 " + std::to_string(程序->functions.size())
                     + " 个函数",
                 "GG");

        输出文本("【阶段三】语义分析 ...", "青");
        SemanticAnalyzer 语义分析器;
        语义分析器.analyze(*程序);
        输出文本("通过", "GG");

        输出文本("【阶段四】字节码生成 ...", "青");
        BytecodeGenerator 生成器(语义分析器.getSymbolTable());
        BytecodeProgram 字节码 = 生成器.generate(*程序);

#ifdef _DEBUG
        字节码.print();
#endif

        输出文本("完成，共 " + std::to_string(字节码.code.size())
                     + " 字节的字节码",
                 "GG");

#ifdef OPTIMIZATION
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
    std::ifstream 文件(路径);
    if (!文件.is_open()) {
        输出文本("错误：无法打开文件 '" + 路径 + "'", "RR");
        return;
    }
    std::stringstream 缓冲区;
    缓冲区 << 文件.rdbuf();
    string 源码 = 缓冲区.str();

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
