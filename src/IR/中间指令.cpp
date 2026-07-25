#include "IR/中间指令.h"

int TACMovI::accept(TACVisitor& v) { return v.visit(*this); }
int TACMovS::accept(TACVisitor& v) { return v.visit(*this); }
int TACMov::accept(TACVisitor& v) { return v.visit(*this); }
int TACBinary::accept(TACVisitor& v) { return v.visit(*this); }
int TACJmp::accept(TACVisitor& v) { return v.visit(*this); }
int TACJif::accept(TACVisitor& v) { return v.visit(*this); }
int TACParm::accept(TACVisitor& v) { return v.visit(*this); }
int TACCall::accept(TACVisitor& v) { return v.visit(*this); }
int TACPrint::accept(TACVisitor& v) { return v.visit(*this); }
int TACRet::accept(TACVisitor& v) { return v.visit(*this); }
int TACHalt::accept(TACVisitor& v) { return v.visit(*this); }
int TACNop::accept(TACVisitor& v) { return v.visit(*this); }

int TACProgram::addConstant(int value) {
    for (size_t i = 0; i < constants.size(); i++)
        if (constants[i] == value) return static_cast<int>(i);
    constants.push_back(value);
    return static_cast<int>(constants.size() - 1);
}

int TACProgram::addString(const std::string& value) {
    for (size_t i = 0; i < strings.size(); i++)
        if (strings[i] == value) return static_cast<int>(i);
    strings.push_back(value);
    return static_cast<int>(strings.size() - 1);
}