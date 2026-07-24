#pragma once
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include "Console.h"

using std::mt19937;
using std::random_device;
using std::string;
using std::uniform_int_distribution;

void 基本界面();
string 输入();
void 交互与执行();
void 编译并运行(const string& 源码);
void 运行文件(const string& 路径);

inline string 获取系统用户名()
{
    const char* name = arch::getUser();
    if (name != nullptr && strlen(name) > 0) return string(name);
    return string("User");
}

inline int 生成随机数(const int 左边, const int 右边)
{
    random_device 种子;
    mt19937 引擎(种子());
    uniform_int_distribution<> 配置(左边, 右边);
    return int(配置(引擎));
}
