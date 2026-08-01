#pragma once
// arch/CommandLine_linux.h — Linux 平台命令行参数

#include <string>
#include <vector>

namespace arch {
namespace detail {

inline std::vector<std::string> linuxCommandLine(int argc, char** argv)
{
	std::vector<std::string> 参数;
	for (int i = 0; i < argc; i++) 参数.push_back(argv[i]);
	return 参数;
}

} // namespace detail
} // namespace arch
