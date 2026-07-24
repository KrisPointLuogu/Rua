// arch/Console_windows.cpp — Windows 平台终端控制

#include "Console.h"

namespace arch {

void setConsoleTitle(const char* title) { detail::winSetTitle(title); }

const char* getUser() { return detail::winGetUser(); }

void initConsole() { detail::winInitConsole(); }

} // namespace arch
