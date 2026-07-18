#pragma once
#include "异常上报.h"
#include <string>

using std::string;

enum 信息内容 {
    ab没有闭合,
    不存在的命令,
    非法UTF8起始字节,
    UTF8序列不完整,
    非法的UTF8续字节,
    码点大小超出Unicode最大值,
    过长编码,
    意外的代理码点
};

/*
* 信息：参照enum枚举类型的"信息内容"进行传入。
* 行位置_：出错的行位置，传入-1最终将表示不适用。
* 列位置_：出错的列位置，传入-1最终将表示不适用。
*/
inline void 信息上报(const 信息内容 信息, const int 行位置_, const int 列位置_) {
    string 报出信息 = "";

    switch(信息){
    case ab没有闭合:
        报出信息 = "字符串没有闭合呐"; break;
    case 不存在的命令:
        报出信息 = "命令不存在呢"; break;
    case 非法UTF8起始字节:
        报出信息 = "有无法解析的不合理UTF8起始字节..."; break;
    case UTF8序列不完整:
        报出信息 = "需要解析的UTF8字符串可能断掉了呢"; break;
    case 非法的UTF8续字节:
        报出信息 = "需要解析的UTF8字符串内容错了呢"; break;
    case 码点大小超出Unicode最大值:
        报出信息 = "嗯哼？UTF8字符串里有一个地方解析出的大小不对唉"; break;
    case 过长编码:
        报出信息 = "emm...这个UTF8字符...不太对哦..."; break;
    case 意外的代理码点:
        报出信息 = "发现一个UTF8字符呢，ta写了代理，但是解析不了呢"; break;
    }

    异常处理(报出信息, 行位置_, 列位置_,警告);
}