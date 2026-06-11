#pragma once
#include "异常上报.h"
#include <string>

using std::string;

enum 信息内容 {
    ab没有闭合,
};

void 信息上报(const 信息内容 信息, const int 行位置_, const int 列位置_) {
    string 报出信息 = "";

    if (信息 == ab没有闭合) 报出信息 = "字符串没有闭合呐";
    // else if()

    异常处理(报出信息, 行位置_, 列位置_,警告);
}