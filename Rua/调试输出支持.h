#pragma once
#include <unordered_map>
#include <iostream>
#include <string>

using std::unordered_map;
using std::string;

void 调试输出(
    string 信息,
    const string& 颜色 = "WW",
    bool 换行 = true,
    bool 前缀 = true
) {

    if (前缀) 信息 = "\033[93m[DEBUG/调试输出] \033[0m" + 信息;
    
    static const unordered_map<string, const char*> 颜色表 = {
        // 基础色
        {"R", "\033[31m"},
        {"G", "\033[32m"},
        {"B", "\033[34m"},
        {"Y", "\033[33m"},

        // 亮色
        {"RR", "\033[91m"},
        {"GG", "\033[92m"},
        {"BB", "\033[94m"},
        {"YY", "\033[93m"},
        {"WW", "\033[97m"},

        // 背景色
        {"黑-背景", "\033[40m"},
        {"红-背景", "\033[41m"},
        {"绿-背景", "\033[42m"}
    };

    string 输出缓冲区;
    输出缓冲区.reserve(信息.size() + 32);

    auto 颜色查找结果 = 颜色表.find(颜色);
    if (颜色查找结果 != 颜色表.end()) {
        输出缓冲区 += 颜色查找结果->second;
    }

    输出缓冲区 += 信息;
    输出缓冲区 += "\033[0m";
    if (换行) 输出缓冲区 += "\n";

    fputs(输出缓冲区.c_str(), stdout);
}