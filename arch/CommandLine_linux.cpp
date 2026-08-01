// arch/CommandLine_linux.cpp — Linux 平台实现

#include "CommandLine.h"

namespace arch {

std::vector<std::string> getCommandLineArgs(int argc, char** argv)
{
	return detail::linuxCommandLine(argc, argv);
}

} // namespace arch
