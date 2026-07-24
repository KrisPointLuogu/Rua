#pragma once
// arch/JITPlatform.h — JIT 可执行内存管理（平台无关接口）
// #ifdef 分支仅在此文件中，其余代码不使用平台宏

#include <cstddef>
#include <cstdint>

#ifdef _LINUX
#include "JITPlatform_linux.h"
#elif defined(_WIN)
#include "JITPlatform_windows.h"
#endif

namespace arch {

// 分配可执行内存（RWX），失败返回 nullptr
uint8_t* allocateExecutableMemory(size_t size);

// 调整内存权限
// prot: 0=无权限, 1=R, 2=W, 4=X, 组合如 5=RX, 7=RWX
bool protectExecutableMemory(uint8_t* ptr, size_t size, int prot);

// 获取系统页大小
size_t getPageSize();

} // namespace arch
