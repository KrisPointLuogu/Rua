#pragma once
// arch/FileSystem_linux.h — Linux 平台文件读取

#include <fstream>
#include <sstream>
#include <string>

namespace arch {
namespace detail {

inline bool linuxReadFile(const std::string& path, std::string& content)
{
	std::ifstream 文件(path, std::ios::binary);
	if (!文件.is_open()) return false;
	std::stringstream 缓冲区;
	缓冲区 << 文件.rdbuf();
	content = 缓冲区.str();
	return true;
}

inline bool linuxWriteFile(const std::string& path, const std::string& content)
{
	std::ofstream 文件(path, std::ios::binary | std::ios::trunc);
	if (!文件.is_open()) return false;
	文件.write(content.data(), static_cast<std::streamsize>(content.size()));
	return static_cast<bool>(文件);
}

} // namespace detail
} // namespace arch
