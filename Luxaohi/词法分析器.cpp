#define _CRT_SECURE_NO_WARNINGS
#include "词法分析器.h"
#include "工具合集.h"
#include <vector>
#include <string>
#include <cctype>

using std::string;
using std::vector;

// 构造函数
词法分析器类::令牌::令牌(令牌类型 t, string v, int l, int c) : 类型_(t), 内容_(std::move(v)), 行位置_(l), 列位置_(c) {}
词法分析器类::词法分析器类(const string& 代码) : 代码_(), 位置_(0), 行位置_(1), 列位置_(1) { 代码_ = 代码; }

vector<词法分析器类::令牌> 词法分析器类::分析() {
	// 定义令牌动态数组
	vector<令牌> 令牌列表;

	// 当前位置小于代码长度的时候运行
	while (位置_ < 代码_.size()) {
		// -128~127 转 0~255
		unsigned char 当前 = static_cast<unsigned char>(代码_[位置_]);

		// 判断空白
		if (isspace(当前)) {
			if (代码_[位置_] == '\n') {
				// 换行符
				令牌列表.emplace_back(换行符, "\n", 行位置_, 列位置_);
				行位置_++; 列位置_ = 1;
			}
			else 列位置_++; // 不是换行符的情况，去读下一个去

			位置_++;
			continue;
		}

		// 这里是注释相关的
		if(位置_+1 < 代码_.size()){
			// 防越界

			if (代码_[位置_] == '#' && 代码_[位置_ + 1] != '*') {
				// 单行注释

				位置_++; 列位置_++;
				while (位置_ < 代码_.size() && 代码_[位置_]!='\n') 位置_++; 列位置_++; // 本行都是注释，所以把位置推到下一行
				continue;
			}
			else if (代码_[位置_] == '#' && 代码_[位置_ + 1] == '*') {
				// 多行注释
				
				位置_++; 列位置_++;
				返回内容 结果 = ab匹配(代码_,位置_,行位置_,列位置_,'*','*');
				if (结果 == ab没有闭合) ;
				continue;
			}
		}

	}
}