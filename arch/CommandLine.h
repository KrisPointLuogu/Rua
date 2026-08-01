#pragma once
// arch/CommandLine.h — 命令行参数（平台无关接口）

#include <string>
#include <vector>

#ifdef _LINUX
#include "CommandLine_linux.h"
#elif defined(_WIN)
#include "CommandLine_windows.h"
#endif

namespace arch {

// 获取命令行参数，全部转换为 UTF-8 编码
// Windows 的 argv 为 ANSI 代码页，中文路径需经宽字符命令行重新转换
std::vector<std::string> getCommandLineArgs(int argc, char** argv);

} // namespace arch
