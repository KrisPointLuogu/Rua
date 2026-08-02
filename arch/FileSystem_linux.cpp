// arch/FileSystem_linux.cpp — Linux 平台实现

#include "FileSystem.h"

namespace arch {

bool readFile(const std::string& path, std::string& content)
{
	return detail::linuxReadFile(path, content);
}

bool writeFile(const std::string& path, const std::string& content)
{
	return detail::linuxWriteFile(path, content);
}

} // namespace arch
