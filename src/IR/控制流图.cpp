#include "IR/控制流图.h"
#include <algorithm>
#include <cassert>
#include <iostream>

void CFG::buildFromTAC(std::vector<std::unique_ptr<TACInst>>& linearTAC) {
    blocks.clear();
    entryBlock = nullptr;

    if (linearTAC.empty()) return;

    struct BlockInfo {
        int id;
        int startIdx, endIdx;
    };

    std::vector<int> blockStarts;
    blockStarts.push_back(0);

    auto isBranch = [](TACInst* inst) -> bool {
        auto op = inst->getOpcode();
        return op == TACOpcode::JMP || op == TACOpcode::JIF || op == TACOpcode::RET
            || op == TACOpcode::HALT;
    };

    for (int i = 0; i < static_cast<int>(linearTAC.size()); i++) {
        auto* inst = linearTAC[i].get();
        if (isBranch(inst) && i + 1 < static_cast<int>(linearTAC.size())) {
            blockStarts.push_back(i + 1);
        }
        if (inst->blockId >= 0 && inst->blockId != i) {
            blockStarts.push_back(i);
        }
    }

    std::sort(blockStarts.begin(), blockStarts.end());
    blockStarts.erase(std::unique(blockStarts.begin(), blockStarts.end()), blockStarts.end());

    int blockId = 0;
    for (size_t si = 0; si < blockStarts.size(); si++) {
        int start = blockStarts[si];
        int end = (si + 1 < blockStarts.size()) ? blockStarts[si + 1]
                                                 : static_cast<int>(linearTAC.size());

        auto block = std::make_unique<BasicBlock>(blockId++);
        for (int j = start; j < end; j++) {
            linearTAC[j]->blockId = block->id;
            block->instructions.push_back(std::move(linearTAC[j]));
        }
        blocks.push_back(std::move(block));
    }

    linearTAC.clear();

    entryBlock = blocks.empty() ? nullptr : blocks[0].get();

    for (auto& block : blocks) {
        if (block->instructions.empty()) continue;
        auto* last = block->instructions.back().get();
        auto op = last->getOpcode();

        if (op == TACOpcode::JMP) {
            auto* jmp = static_cast<TACJmp*>(last);
            auto* target = getBlock(jmp->targetBlock);
            if (target) {
                block->successors.push_back(target);
                target->predecessors.push_back(block.get());
            }
        } else if (op == TACOpcode::JIF) {
            auto* jif = static_cast<TACJif*>(last);
            auto* tgt = getBlock(jif->targetBlock);
            auto* fall = getBlock(jif->fallBlock);
            if (tgt) {
                block->successors.push_back(tgt);
                tgt->predecessors.push_back(block.get());
            }
            if (fall) {
                block->successors.push_back(fall);
                fall->predecessors.push_back(block.get());
            }
        } else if (op == TACOpcode::RET || op == TACOpcode::HALT) {
            // no successors
        } else {
            int nextId = block->id + 1;
            if (nextId < static_cast<int>(blocks.size())) {
                auto* next = blocks[nextId].get();
                block->successors.push_back(next);
                next->predecessors.push_back(block.get());
            }
        }
    }
}

void CFG::recomputeFromScratch(std::vector<std::unique_ptr<TACInst>>& linearTAC) {
    for (auto& block : blocks) {
        for (auto& inst : block->instructions)
            linearTAC.push_back(std::move(inst));
        block->instructions.clear();
    }
    blocks.clear();
    entryBlock = nullptr;
    buildFromTAC(linearTAC);
}

BasicBlock* CFG::getBlock(int id) const {
    for (auto& b : blocks)
        if (b->id == id) return b.get();
    return nullptr;
}

void CFG::removeBlock(int id) {
    auto* target = getBlock(id);
    if (!target) return;

    for (auto* pred : target->predecessors) {
        auto& succs = pred->successors;
        succs.erase(std::remove(succs.begin(), succs.end(), target), succs.end());
    }
    for (auto* succ : target->successors) {
        auto& preds = succ->predecessors;
        preds.erase(std::remove(preds.begin(), preds.end(), target), preds.end());
    }

    auto it = std::remove_if(blocks.begin(), blocks.end(),
                              [id](const auto& b) { return b->id == id; });
    blocks.erase(it, blocks.end());
}

void CFG::print() const {
    std::cout << "====== CFG ======\n";
    for (auto& block : blocks) {
        std::cout << "Block B" << block->id << " (loopDepth=" << block->loopDepth
                  << ", domDepth=" << block->domDepth << "):\n";
        std::cout << "  preds:";
        for (auto* p : block->predecessors) std::cout << " B" << p->id;
        std::cout << "\n  succs:";
        for (auto* s : block->successors) std::cout << " B" << s->id;
        std::cout << "\n";
        if (block->idom)
            std::cout << "  idom: B" << block->idom->id << "\n";
        for (auto& inst : block->instructions) {
            std::cout << "    ";
            switch (inst->getOpcode()) {
            case TACOpcode::MOVI: {
                auto* m = static_cast<TACMovI*>(inst.get());
                std::cout << "MOVI v" << m->rd.index << ", #" << m->constVal;
                break;
            }
            case TACOpcode::MOVS: {
                auto* m = static_cast<TACMovS*>(inst.get());
                std::cout << "MOVS v" << m->rd.index << ", str[" << m->stringIdx << "]";
                break;
            }
            case TACOpcode::MOV: {
                auto* m = static_cast<TACMov*>(inst.get());
                std::cout << "MOV v" << m->rd.index << ", v" << m->rs.index;
                break;
            }
            case TACOpcode::ADD: case TACOpcode::SUB: case TACOpcode::MUL:
            case TACOpcode::DIV: case TACOpcode::MOD:
            case TACOpcode::EQ: case TACOpcode::NE:
            case TACOpcode::LT: case TACOpcode::GT: case TACOpcode::LE: case TACOpcode::GE: {
                auto* b = static_cast<TACBinary*>(inst.get());
                static const char* names[] = {"NOP","MOVI","MOVS","MOV","ADD","SUB","MUL","DIV","MOD",
                    "EQ","NE","LT","GT","LE","GE","JMP","JIF","CALL","RET","PUSH","PRINT","HALT"};
                std::cout << names[(int)b->op] << " v" << b->rd.index
                          << ", v" << b->rs1.index << ", v" << b->rs2.index;
                break;
            }
            case TACOpcode::JMP: {
                auto* j = static_cast<TACJmp*>(inst.get());
                std::cout << "JMP B" << j->targetBlock;
                break;
            }
            case TACOpcode::JIF: {
                auto* j = static_cast<TACJif*>(inst.get());
                std::cout << "JIF v" << j->cond.index << ", B" << j->targetBlock
                          << ", fall B" << j->fallBlock;
                break;
            }
            case TACOpcode::PUSH: {
                auto* p = static_cast<TACParm*>(inst.get());
                std::cout << "PUSH v" << p->rs.index;
                break;
            }
            case TACOpcode::CALL: {
                auto* c = static_cast<TACCall*>(inst.get());
                std::cout << "CALL " << c->funcName << " -> v" << c->rd.index;
                break;
            }
            case TACOpcode::PRINT: {
                auto* p = static_cast<TACPrint*>(inst.get());
                std::cout << "PRINT v" << p->rs.index;
                break;
            }
            case TACOpcode::RET: {
                auto* r = static_cast<TACRet*>(inst.get());
                std::cout << "RET v" << r->rs.index;
                break;
            }
            case TACOpcode::HALT:
                std::cout << "HALT";
                break;
            default: break;
            }
            std::cout << "\n";
        }
    }
    std::cout << "=================\n";
}