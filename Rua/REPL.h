#pragma once
#include <random>
#include <string>
#include <cstring>
#include <cstdlib>

#ifdef _WIN32
    #include <windows.h>
#endif

#ifdef _DEBUG
#else
    #pragma comment(lib, "advapi32.lib")
#endif

using std::string;
using std::mt19937;
using std::random_device;
using std::uniform_int_distribution;

// 函数声明
void 基本界面();
string 输入();
void 交互与执行();

// 私有工具函数定义
inline string 获取系统用户名() {
#if defined(_WIN32)
    char 名字[256];
    DWORD 大小 = 256;
    // 获取并返回string类型用户名
    if (GetUserNameA(名字, &大小)) return string(名字);
    return string("Administrator");

#elif defined(__linux__) || defined(__APPLE__)
    char* 名字 = getenv("USER");

    // 返回string类型用户名
    if (名字 != nullptr && strlen(名字) > 0) return string(名字);
    return string("User");

#endif

}

inline int 生成随机数(const int 左边, const int 右边) {
    // 获取种子
    random_device 种子;

    // 用梅森旋转算法引擎
    mt19937 引擎(种子());
    uniform_int_distribution<> 配置(左边, 右边);

    // 返回int类型决定的随机数
    return int(配置(引擎));
}