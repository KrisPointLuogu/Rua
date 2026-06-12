#pragma once
#include<string>

using std::string;
using std::u32string;

struct 全局 {
    string 用户名;
    bool 高精度数学计算模式;
};

extern 全局 全局内容;

// 返回内容的定义
enum 返回内容 {
    成功,
    ab没闭合
};

// 工具函数的声明
void 初始化窗口();
string 获取系统用户名();
int 生成随机数(const int 左边, const int 右边);
返回内容 ab匹配(const u32string& 代码_, size_t& 位置_, int& 行位置_, int& 列位置_, const char32_t a边, const char32_t b边, bool 回退 = false);
u32string ab匹配字符串令牌(const u32string& 代码_, size_t& 位置_, int& 行位置_, int& 列位置_, const char32_t a边, const char32_t b边);
u32string 读取可能的标识符或关键词令牌(const u32string& 代码_, size_t& 位置_, int& 列位置_);
bool 可用于标识符字符(char32_t 字符_);