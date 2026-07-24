#pragma once
#include <string>

using std::string;

/*
 * 无参数
 */
inline string 平台检测()
{
    string 系统;
    string 架构;
#if defined(_WIN64)
    系统 = "Windows";
    架构 = "x86_64";
#elif defined(_WIN32)
    系统 = "Windows";
    架构 = "x86";

#elif defined(__linux__)
    系统 = "Linux";
#if defined(__x86_64__)
    架构 = "x86_64";
#elif defined(__i386__)
    架构 = "x86";
#elif defined(__aarch64__)
    架构 = "ARM64";
#endif

#elif defined(__APPLE__)
    系统 = "macOS";
#if defined(__x86_64__)
    架构 = "x86_64";
#elif defined(__arm64__)
    架构 = "ARM64";
#endif

#elif defined(__unix__)
    系统 = "Unix";
#if defined(__x86_64__)
    架构 = "x86_64";
#elif defined(__i386__)
    架构 = "x86";
#elif defined(__aarch64__)
    架构 = "ARM64";
#else
    架构 = "???";
#endif

#elif defined(__ANDROID__)
    系统 = "Android";
#if defined(__aarch64__)
    架构 = "ARM64";
#elif defined(__arm__)
    架构 = "ARM32";
#elif defined(__x86_64__)
    架构 = "x86_64";
#elif defined(__i386__)
    架构 = "x86";
#endif

#else
    系统 = "???";
    架构 = "???";
#endif

    return "基于 " + 架构 + " 架构的 " + 系统 + " 系统";
}