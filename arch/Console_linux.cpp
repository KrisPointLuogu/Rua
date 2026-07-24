// arch/Console_linux.cpp — Linux 平台终端控制

#include "Console.h"

namespace arch {

void setConsoleTitle(const char* title) { detail::linuxSetTitle(title); }

const char* getUser() { return detail::linuxGetUser(); }

void initConsole() { detail::linuxInitConsole(); }

} // namespace arch
