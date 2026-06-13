#pragma once
#include "异常上报.h"
#include <string>

using std::string;

enum 信息内容 {
    ab没有闭合,
    不存在的命令
};

inline void 信息上报(const 信息内容 信息, const int 行位置_, const int 列位置_) {
    string 报出信息 = "";

    switch(信息){
    case ab没有闭合:
        报出信息 = "字符串没有闭合呐"; break;
    case 不存在的命令:
        报出信息 = "命令不存在呢"; break;
    }

    异常处理(报出信息, 行位置_, 列位置_,警告);
}