#ifndef 异常上报_H
#define 异常上报_H
#include "输出彩色支持.h"
#include <stdexcept>
#include <string>
using std::string;
using std::runtime_error;

inline void 异常处理(
    const string& 信息, 
    int 等级 = 5, 
    bool 需要输出 = true,
    bool 报出 = true,
    const string& 角色="刘小黑"
) {

    string 信息拼接 = string("[") + 角色 + "]" + 信息;
    
    switch (等级) {
    case 1:
        //完成
        if (需要输出 == true) { 输出文本(信息拼接, "GG"); }
        if (报出 == true) { throw runtime_error("[√]PASS完成: " + 信息拼接); }
        break;
    case 2:
        //普通提示
        if (需要输出 == true) { 输出文本(信息拼接, "BB"); }
        if (报出 == true) { throw runtime_error("[i]INFO信息: " + 信息拼接); }
        break;

    case 3:
        //警告
        if (需要输出 == true) { 输出文本(信息拼接, "YY"); }
        if (报出 == true) { throw runtime_error("[!]WARNING警告: " + 信息拼接); }
        break;

    case 4:
        //错误
        if (需要输出 == true) { 输出文本(信息拼接, "RR"); }
        if (报出 == true) { throw runtime_error("[x]ERROR错误: " + 信息拼接); }
        break;

    default:
        //如果忘了写"等级"才会执行，不故意使用这个等级
        if (报出 == true) { throw runtime_error("[A/N]WHAT未知: " + 信息拼接); }
        break;
    }
}

#endif
