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

using std::mt19937;
using std::random_device;
using std::string;
using std::uniform_int_distribution;

void 基本界面();
string 输入();
void 交互与执行();
void 编译并运行(const string &源码);
void 运行文件(const string &路径);

inline string 获取系统用户名()
{
#if defined(_WIN32)
    char 名字[256];
    DWORD 大小 = 256;
    if (GetUserNameA(名字, &大小))
        return string(名字);
    return string("Administrator");

#elif defined(__linux__) || defined(__APPLE__)
    char *名字 = getenv("USER");
    if (名字 != nullptr && strlen(名字) > 0)
        return string(名字);
    return string("User");

#endif
}

inline int 生成随机数(const int 左边, const int 右边)
{
    random_device 种子;
    mt19937 引擎(种子());
    uniform_int_distribution<> 配置(左边, 右边);
    return int(配置(引擎));
}
