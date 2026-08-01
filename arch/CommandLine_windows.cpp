// arch/CommandLine_windows.cpp — Windows 平台实现

#include "CommandLine.h"

namespace arch {

std::vector<std::string> getCommandLineArgs(int argc, char** argv)
{
	// 忽略传入的 argc/argv（ANSI 编码不可用），改用宽字符命令行
	(void)argc;
	(void)argv;
	return detail::winCommandLine();
}

} // namespace arch
