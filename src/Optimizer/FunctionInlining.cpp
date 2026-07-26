#include <iostream>
#include "Optimizer/PassManager.h"

bool FunctionInlining::run(TACProgram& program, int funcIdx)
{
    if (funcIdx < 0 || funcIdx >= static_cast<int>(program.functions.size()))
        return false;

    auto& caller = program.functions[funcIdx];
    bool changed = false;

    for (int i = static_cast<int>(caller.instructions.size()) - 1; i >= 0;
         i--) {
        auto* inst = caller.instructions[i].get();
        if (inst->getOpcode() != TACOpcode::CALL) continue;

        auto* call = static_cast<TACCall*>(inst);
        int calleeIdx = call->funcIdx;

        if (calleeIdx == funcIdx) continue;
        if (call->argCount != program.functions[calleeIdx].paramCount) continue;

        int calleeSize = static_cast<int>(
            program.functions[calleeIdx].instructions.size());
        if (calleeSize > 50) continue;

        bool isRecursive = false;
        for (auto& calleeInst : program.functions[calleeIdx].instructions) {
            if (calleeInst->getOpcode() == TACOpcode::CALL) {
                auto* cc = static_cast<TACCall*>(calleeInst.get());
                if (cc->funcIdx == calleeIdx) {
                    isRecursive = true;
                    break;
                }
            }
        }
        if (isRecursive) continue;

        std::vector<TACValue> args;
        int pushIdx = i - 1;
        while (pushIdx >= 0 && static_cast<int>(args.size()) < call->argCount) {
            if (caller.instructions[pushIdx]->getOpcode() == TACOpcode::PUSH) {
                auto* p
                    = static_cast<TACParm*>(caller.instructions[pushIdx].get());
                args.insert(args.begin(), p->rs);
            }
            pushIdx--;
        }

        std::vector<std::unique_ptr<TACInst>> inlinedBody;
        for (auto& inst : program.functions[calleeIdx].instructions)
            inlinedBody.push_back(inst->clone());

        int paramRegBase = 1;
        int nonParamBase = paramRegBase + static_cast<int>(args.size());
        auto remapVar = [&](TACValue& v) {
            if (v.kind == TACValueKind::VAR && v.index >= nonParamBase) {
                v.kind = TACValueKind::TEMP;
                v.index = 300 + (v.index - nonParamBase);
            }
        };

        for (auto& inst : inlinedBody) {
            auto op = inst->getOpcode();
            if (op == TACOpcode::MOV) {
                auto* m = static_cast<TACMov*>(inst.get());
                if (m->rs.kind == TACValueKind::VAR) {
                    int varIdx = m->rs.index;
                    if (varIdx >= paramRegBase && varIdx < nonParamBase) {
                        m->rs = args[varIdx - paramRegBase];
                    } else {
                        remapVar(m->rs);
                    }
                }
                remapVar(m->rd);
            } else if (op == TACOpcode::MOVI) {
                auto* m = static_cast<TACMovI*>(inst.get());
                if (m->rd.kind == TACValueKind::VAR) {
                    int varIdx = m->rd.index;
                    if (varIdx >= paramRegBase && varIdx < nonParamBase) {
                        inst = std::make_unique<TACMov>(
                            m->rd, args[varIdx - paramRegBase]);
                    } else {
                        remapVar(m->rd);
                    }
                }
            } else if (op == TACOpcode::ADD || op == TACOpcode::SUB
                       || op == TACOpcode::MUL || op == TACOpcode::DIV
                       || op == TACOpcode::MOD || op == TACOpcode::EQ
                       || op == TACOpcode::NE || op == TACOpcode::LT
                       || op == TACOpcode::GT || op == TACOpcode::LE
                       || op == TACOpcode::GE) {
                auto* b = static_cast<TACBinary*>(inst.get());
                if (b->rs1.kind == TACValueKind::VAR) {
                    int varIdx = b->rs1.index;
                    if (varIdx >= paramRegBase && varIdx < nonParamBase)
                        b->rs1 = args[varIdx - paramRegBase];
                    else
                        remapVar(b->rs1);
                }
                if (b->rs2.kind == TACValueKind::VAR) {
                    int varIdx = b->rs2.index;
                    if (varIdx >= paramRegBase && varIdx < nonParamBase)
                        b->rs2 = args[varIdx - paramRegBase];
                    else
                        remapVar(b->rs2);
                }
                remapVar(b->rd);
            } else if (op == TACOpcode::JIF) {
                auto* j = static_cast<TACJif*>(inst.get());
                remapVar(j->cond);
            } else if (op == TACOpcode::PRINT) {
                auto* p = static_cast<TACPrint*>(inst.get());
                remapVar(p->rs);
            } else if (op == TACOpcode::PUSH) {
                auto* p = static_cast<TACParm*>(inst.get());
                remapVar(p->rs);
            } else if (op == TACOpcode::RET) {
                auto* r = static_cast<TACRet*>(inst.get());
                inst = std::make_unique<TACMov>(call->rd, r->rs);
            } else if (op == TACOpcode::CALL) {
                // nested calls - skip for simplicity
            }
        }

        int removeStart = i - call->argCount - 1;
        int removeCount = i + 1 - removeStart;
        int oldSize = static_cast<int>(caller.instructions.size());
        caller.instructions.erase(caller.instructions.begin() + removeStart,
                                  caller.instructions.begin() + i + 1);

        int inlinedSize = static_cast<int>(inlinedBody.size());
        caller.instructions.insert(caller.instructions.begin() + removeStart,
                                   std::make_move_iterator(inlinedBody.begin()),
                                   std::make_move_iterator(inlinedBody.end()));

        std::vector<int> oldToNew(oldSize);
        int netShift = inlinedSize - removeCount;
        for (int k = 0; k < oldSize; k++) {
            if (k < removeStart) {
                oldToNew[k] = k;
            } else if (k <= i) {
                oldToNew[k] = removeStart + (k - removeStart);
            } else {
                oldToNew[k] = k + netShift;
            }
        }
        fixJumpTargets(caller, oldToNew);

        changed = true;
        break;
    }
    return changed;
}
