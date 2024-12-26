#include <llvm/IR/Function.h>
#include <llvm/Pass.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/CFG.h>

#include "Analysis.h"
using namespace llvm;
static ManagedStatic<LLVMContext> GlobalContext;
static LLVMContext &getGlobalContext() { return *GlobalContext; }

class PointToAnalysis : public ModulePass {
public:

    static char ID;
    PointToAnalysis() : ModulePass(ID) {} 

    bool runOnModule(Module &M) override {
        // 这个位置以模块为粒度进行run
        DataflowResult result;          // 存储结果
        LLVMContext &Context = getGlobalContext();


        Analysis(M, &result);
        result.printData();
        return false;
    }
};



