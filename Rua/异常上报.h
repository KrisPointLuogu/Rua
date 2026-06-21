#pragma once
#include "输出彩色支持.h"
#include <stdexcept>
#include <string>

using std::string;
using std::to_string;
using std::runtime_error;

enum 异常类型 {
    完成 = 1,
    提示 = 2,
    警告 = 3,
    错误 = 4,
    未知 = 5
};

inline void 异常处理(
    const string& 信息,
    const int 行位置_,
    const int 列位置_,
    异常类型 等级 = 未知,
    bool 需要输出 = true,
    bool 报出 = true,
    const string& 角色="刘小黑"
) {

    static string 信息内容[5];

    信息内容[1] = string("[") + 角色 + "]" + 信息;
    if (行位置_ != -1) 信息内容[2] = "位于行：" + to_string(行位置_);
    else 信息内容[2] = "位于行：不适用";
    if (列位置_ != -1) 信息内容[3] = "位于列：" + to_string(列位置_);
    else 信息内容[3] = "位于列：不适用";
    
    switch (等级) {
    case 完成:
        //完成
        if (需要输出 == true) {
            输出文本(信息内容[1], "GG");
            输出文本(信息内容[2], "GG");
            输出文本(信息内容[3], "GG");
        }
        if (报出 == true) { throw runtime_error("[√]PASS完成: " + 信息内容[1] + "\n" + 信息内容[2] + "\n" + 信息内容[3]); }
        break;

    case 提示:
        //普通提示
        if (需要输出 == true) {
            输出文本(信息内容[1], "BB");
            输出文本(信息内容[2], "BB");
            输出文本(信息内容[3], "BB");
        }
        if (报出 == true) { throw runtime_error("[i]INFO信息: " + 信息内容[1] + "\n" + 信息内容[2] + "\n" + 信息内容[3]); }
        break;

    case 警告:
        //警告
        if (需要输出 == true) {
            输出文本(信息内容[1], "YY");
            输出文本(信息内容[2], "YY");
            输出文本(信息内容[3], "YY");
        }
        if (报出 == true) { throw runtime_error("[!]WARNING警告: " + 信息内容[1] + "\n" + 信息内容[2] + "\n" + 信息内容[3]); }
        break;

    case 错误:
        //错误
        if (需要输出 == true) {
            输出文本(信息内容[1], "RR");
            输出文本(信息内容[2], "RR");
            输出文本(信息内容[3], "RR");
        }
        if (报出 == true) { throw runtime_error("[x]ERROR错误: " + 信息内容[1] + "\n" + 信息内容[2] + "\n" + 信息内容[3]); }
        break;

    default:
        //如果忘了写"等级"才会执行，不故意使用这个等级
        if (报出 == true) { throw runtime_error("[A/N]WHAT未知: " + 信息内容[1] + "\n" + 信息内容[2] + "\n" + 信息内容[3]); }
        break;
    }
}