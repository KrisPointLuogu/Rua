#pragma once
#include<string>
using std::string;

struct 全局 {
    string 用户名;
    bool 高精度数学计算模式;
};

extern 全局 全局内容;

// 工具函数的声明
void 初始化窗口();
string 获取系统用户名();
int 生成随机数(const int& 左边, const int& 右边);