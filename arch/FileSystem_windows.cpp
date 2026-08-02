// arch/FileSystem_windows.cpp — Windows 平台实现

#include "FileSystem.h"

namespace arch {

bool readFile(const std::string& path, std::string& content)
{
	return detail::winReadFile(path, content);
}

bool writeFile(const std::string& path, const std::string& content)
{
	return detail::winWriteFile(path, content);
}

} // namespace arch
