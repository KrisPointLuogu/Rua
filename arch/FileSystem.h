#pragma once
// arch/FileSystem.h — 文件系统访问（平台无关接口）

#include <string>

#ifdef _LINUX
#include "FileSystem_linux.h"
#elif defined(_WIN)
#include "FileSystem_windows.h"
#endif

namespace arch {

// 读取文本文件全部内容
// 成功返回 true 并把内容写入 内容；失败返回 false
bool readFile(const std::string& path, std::string& content);

} // namespace arch
