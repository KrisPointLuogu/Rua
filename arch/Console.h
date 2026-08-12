#pragma once
// arch/Console.h — 终端控制（平台无关接口）

#if defined(_WIN32)
#include "Console_windows.h"
#elif defined(_LINUX)
#include "Console_linux.h"
#endif

namespace arch {

// 设置终端窗口标题
void setConsoleTitle(const char* title);

// 获取当前用户名（失败返回 nullptr）
const char* getUser();

// 初始化终端（UTF-8、ANSI 转义等）
void initConsole();

} // namespace arch
