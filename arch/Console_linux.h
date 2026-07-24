#pragma once
// arch/Console_linux.h — Linux 平台终端控制

#include <clocale>
#include <cstdio>
#include <cstdlib>

namespace arch {
namespace detail {

inline void linuxSetTitle(const char* title) { printf("\033]0;%s\007", title); }

inline const char* linuxGetUser() { return getenv("USER"); }

inline void linuxInitConsole() { setlocale(LC_ALL, ""); }

} // namespace detail
} // namespace arch
