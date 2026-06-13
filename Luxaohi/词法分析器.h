#pragma once
#include <string>
#include <vector>

using std::string;

class 词法分析器类 {

public:
    // 定义token都有什么类型
    enum 令牌类型 {
        函数关键字, 如果关键字, 那么关键字, 否则关键字, 或者关键字, 返回关键字, 当关键字,
        变量关键字, 数组关键字,
        标识符, 数字, 字符串,
        左括号, 右括号, 左中括号, 右中括号, 左花括号, 右花括号, 逗号, 分号, 等号, 加号, 减号,
        乘号, 除号, 等于, 不等于, 大于, 小于, 模, 乘方, 整除,
        加等于, 减等于, 乘等于, 除等于, 乘方等于, 模等于, 整除等于,
        换行符, 结束
    };

    // 一个token包含什么
    struct 令牌 {
        令牌类型 类型_;
        std::u32string 内容_;
        int 行位置_;
        int 列位置_;
        令牌(令牌类型 t, std::u32string v, int l, int c); // 构造函数，简化写法
    };

    词法分析器类(const string& 代码);
    std::vector<令牌> 分析();

private:
    std::u32string 代码_; // 输入的源代码，这里转UTF-32，这样之后就不用费劲处理中英符号字节大小差异了！
    size_t 位置_;
    int 行位置_;
    int 列位置_;
};