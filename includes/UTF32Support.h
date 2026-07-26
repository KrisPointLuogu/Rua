#pragma once
#include <string>

using std::string;
using std::u32string;

u32string UTF8转UTF32(const string& UTF8);
string UTF32转UTF8(const u32string& UTF32);