#include "main.h"
#include "REPL.h"
#include "Globals.h"
#include "Platform.h"
#include "CommandLine.h"
#include "Bytecode.h"
#include "VM.h"
#include "ColorOutput.h"
#include "FileSystem.h"
#include <vector>

全局 全局内容;

namespace {

void 用法(const char* 程序名)
{
    printf("Rua — 中文编程语言解释器 v0.2.0\n");
    printf("\n用法：\n");
    printf("  %s <源文件.rua>               编译并运行\n", 程序名);
    printf("  %s <源文件.rua> -o <输出.rab>  编译并生成字节码文件，然后运行\n",
           程序名);
    printf("  %s <源文件.rua> -c -o <输出.rab>  只编译生成字节码文件，不运行\n",
           程序名);
    printf("  %s <源文件.rua> -S <文本路径>  生成人类可读字节码文本\n", 程序名);
    printf("  %s --run-rab <文件.rab>        加载并执行字节码文件\n", 程序名);
    printf("  %s -h, --help                 显示此帮助\n", 程序名);
}

} // namespace

int main(int argc, char* argv[])
{
#ifdef _DEBUG
#pragma message("当前处于DEBUG编译模式，调试功能将开启")
#endif

    全局内容.系统信息 = 平台检测();

    try {
        初始化窗口();

        // Windows 下 argv 为 ANSI 编码，统一经由 arch 转为 UTF-8
        std::vector<string> 参数 = arch::getCommandLineArgs(argc, argv);

        if (参数.size() <= 1) {
            交互与执行();
            return 0;
        }

        if (参数[1] == "-h" || 参数[1] == "--help") {
            用法(argv[0]);
            return 0;
        }

        if (参数[1] == "--run-rab") {
            if (参数.size() < 3) {
                用法(argv[0]);
                return 1;
            }
            运行字节码文件(参数[2]);
            return 0;
        }

        // 解析 -o / -c / -S
        string 源文件 = 参数[1];
        string 输出rab;
        string 输出文本路径;
        bool 仅编译 = false;

        for (size_t i = 2; i < 参数.size(); i++) {
            if (参数[i] == "-o") {
                if (i + 1 >= 参数.size()) {
                    printf("错误：-o 需要一个输出文件路径\n");
                    return 1;
                }
                输出rab = 参数[++i];
            } else if (参数[i] == "-S") {
                if (i + 1 >= 参数.size()) {
                    printf("错误：-S 需要一个输出文件路径\n");
                    return 1;
                }
                输出文本路径 = 参数[++i];
            } else if (参数[i] == "-c") {
                仅编译 = true;
            } else {
                printf("错误：未知参数 '%s'\n", 参数[i].c_str());
                用法(argv[0]);
                return 1;
            }
        }

        // 读取源码
        string 源码;
        if (!arch::readFile(源文件, 源码)) {
            输出文本("错误：无法打开文件 '" + 源文件 + "'", "RR");
            return 1;
        }

        输出文本("正在编译 " + 源文件 + " ...", "青");

        // 编译
        BytecodeProgram 字节码;
        if (!编译(源码, 字节码)) return 1;

        // -S：写人类可读文本
        if (!输出文本路径.empty()) {
            string 错误信息;
            if (!字节码.saveText(输出文本路径, 错误信息)) {
                输出文本("错误：" + 错误信息, "RR");
                return 1;
            }
            输出文本("已写出字节码文本: " + 输出文本路径, "GG");
        }

        // -o：写 .rab 字节码文件
        if (!输出rab.empty()) {
            string 错误信息;
            if (!字节码.save(输出rab, 错误信息)) {
                输出文本("错误：" + 错误信息, "RR");
                return 1;
            }
            输出文本("已生成字节码文件: " + 输出rab, "GG");
        }

        // 非仅编译模式：继续执行
        if (!仅编译) {
            try {
                输出文本("【阶段五】虚拟机执行 ...", "青");
                VM 虚拟机;
                虚拟机.run(字节码);
                std::cout << std::endl;
                输出文本("程序执行完毕！", "GG");
            } catch (const VMError& e) {
                输出文本(string("运行时错误：") + e.what(), "RR");
            } catch (const std::exception& e) {
                输出文本(string("错误：") + e.what(), "RR");
            }
        }
    } catch (...) {
    }
    return 0;
}
