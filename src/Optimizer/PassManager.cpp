#include "Optimizer/PassManager.h"
#include <iostream>

#ifdef _DEBUG
#include "IR/TACDump.h"
#endif

PassManager::PassManager()
{
    passes.push_back(std::make_unique<ConstantPropagation>());
    passes.push_back(std::make_unique<DeadCodeElimination>());
    // TODO: LICM, LoopUnrolling, FunctionInlining need jump target fixup
    // passes.push_back(std::make_unique<FunctionInlining>());
    // passes.push_back(std::make_unique<LoopInvariantCodeMotion>());
    // passes.push_back(std::make_unique<LoopUnrolling>());
    passes.push_back(std::make_unique<StrengthReduction>());
}

bool PassManager::runAll(TACProgram& program)
{
    bool changed = false;
    for (int iter = 0; iter < MAX_ITERATIONS; iter++) {
        bool iterChanged = false;
        for (auto& pass : passes) {
            for (int i = 0; i < static_cast<int>(program.functions.size());
                 i++) {
                if (pass->run(program, i)) {
                    iterChanged = true;
#ifdef _DEBUG
                    std::cout << "  [" << pass->name() << "] func " << i
                              << " changed:\n";
                    dumpTACFunction(program.functions[i]);
#endif
                }
            }
        }
        if (!iterChanged) break;
        changed = true;
        std::cout << "Optimization pass " << (iter + 1)
                  << " completed, changed\n";
    }
    return changed;
}