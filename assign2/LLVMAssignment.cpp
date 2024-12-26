//===- Hello.cpp - Example code from "Writing an LLVM Pass" ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements two versions of the LLVM "Hello World" pass described
// in docs/WritingAnLLVMPass.html
//
//===----------------------------------------------------------------------===//

#include <llvm/Support/CommandLine.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Support/ToolOutputFile.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Type.h>

#include <llvm/Transforms/Scalar.h>
#include <llvm/Transforms/Utils.h>

#include <llvm/IR/Function.h>
#include <llvm/Pass.h>
#include <llvm/Support/raw_ostream.h>

#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/Bitcode/BitcodeWriter.h>


using namespace llvm;
static ManagedStatic<LLVMContext> GlobalContext;
static LLVMContext &getGlobalContext() { return *GlobalContext; }
/* In LLVM 5.0, when  -O0 passed to clang , the functions generated with clang will
 * have optnone attribute which would lead to some transform passes disabled, like mem2reg.
 */
struct EnableFunctionOptPass: public FunctionPass {
    static char ID;
    EnableFunctionOptPass():FunctionPass(ID){}
    bool runOnFunction(Function & F) override{
        if(F.hasFnAttribute(Attribute::OptimizeNone))
        {
            F.removeFnAttr(Attribute::OptimizeNone);
        }
        return true;
    }
};

char EnableFunctionOptPass::ID=0;

// lineAndNames里面还存了返回指令相关信息，这里控制是否输出
#define SHOW_RET 0

// 全局变量
std::map<int, std::string> lineAndFunc;   // 行数，所在函数名
std::map<int, std::vector<std::string>> lineAndNames; // 行数，可能的被调用函数名
std::map<int, unsigned> lineAndArg;     // 行数，参数索引
std::string funcName;     // 当前正在处理的函数的名字
// 函数声明
void StoreName(int i, std::string str);
void CopeWithArgs(std::string name, Module &M, CallInst *CI);
void CopeWithFunRet(std::string rightName, Module &M, int line);
void CopeWithCall(Value* callee, Module &M, CallInst *CI, int line);
void Analysis(Module &M);
void PrintResult();

bool NamesChanged = true;

void StoreName(int i, std::string str){
    auto it = lineAndNames.find(i);
    if (it != lineAndNames.end()) {
        // 如果存在，直接在对应的vector中插入字符串
        // it->second.push_back(str);
        if (std::find(it->second.begin(), it->second.end(), str) == it->second.end()) {
            // 如果不存在，添加字符串
            it->second.push_back(str);
            NamesChanged = true;
        }
    } else {
        // 如果不存在，先创建一个vector，然后插入字符串，再将键值对插入map
        lineAndNames[i].push_back(str);
        NamesChanged = true;
    }
    return;
}

void CopeWithArgs(std::string name, Module &M, CallInst *CI){
    // 找到这个函数名里面所有调用函数处
    std::vector<int> lines;
    lines.clear();
    for (const auto& pair : lineAndFunc) {
        if (pair.second == name) {
            lines.push_back(pair.first);
        }
    }

    // 根据行数找到参数索引，如果有索引则去找名字
    for (int line : lines) {
        // 在lineAndArg中查找对应的ArgNo
        auto it = lineAndArg.find(line);
        if (it != lineAndArg.end()) {
            // 如果找到了
            unsigned ArgNo = it->second;
            // 找出这次函数调用时传入的参数
            Value *Arg = CI->getOperand(ArgNo);
            CopeWithCall(Arg, M, CI, line);
        }
    }
}

void CopeWithFunRet(std::string rightName, Module &M, int line){
    // 找到函数名对应的函数
    for(Module::iterator fi = M.begin(), fe = M.end(); fi != fe; fi++){
        //遍历所有函数
        Function &F = *fi;
        std::string thisName = F.getName().str();
        if(thisName == rightName){
            // 找到对应函数之后，找到返回指令的位置
            for(Function::iterator bi = F.begin(), be = F.end(); bi != be; bi++){
            //遍历所有块
            BasicBlock &B = *bi;
            for(BasicBlock::iterator ii = B.begin(), ie = B.end(); ii != ie; ii++){
                //遍历所有指令
                Instruction &I = *ii;
                if(ReturnInst *RI = dyn_cast<ReturnInst>(&I)){
                    // 如果是返回指令
                    int ret_line = RI->getDebugLoc().getLine();    // 获取行
                    // 将ret_line行对应的名字存储到line行
                    auto it = lineAndNames.find(-ret_line);
                    if (it != lineAndNames.end()) {
                        // 访问对应的字符串向量
                        std::vector<std::string>& names = it->second;
                        
                        // 遍历字符串向量并进行分析
                        for (const std::string& name : names) {
                            StoreName(line, name);
                        }
                    }
                }
            }
        }
        }
    }
}

void CopeWithCall(Value* callee, Module &M, CallInst *CI, int line){
    //callee是被调用的函数，可能以下情况
    if (PHINode* phiInst = dyn_cast<PHINode>(callee)) {
        // phi结点，递归
        unsigned numIncomingValues = phiInst->getNumIncomingValues();
        // 遍历所有入边值并递归处理他们
        for (unsigned i = 0; i < numIncomingValues; ++i) {
            Value* incomingValue = phiInst->getIncomingValue(i);
            CopeWithCall(incomingValue, M, CI, line);
        }
    }
    else if(Function *F = dyn_cast<Function>(callee)){
        // 直接调用，直接存名字
        if (callee->hasName()) {
            // names.push_back(callee->getName().str());
            std::string name = callee->getName().str();
            StoreName(line, name);
            // 这里应该处理一下这个函数内部需要解决的间接调用，和传参有关系
            CopeWithArgs(name, M, CI);
        }
    }
    else if(Argument *A = dyn_cast<Argument>(callee)){
        // 获取Argument在被调用函数中的索引位置，并将对应信息存储
        unsigned ArgNo = A->getArgNo();
        lineAndArg[line] = ArgNo;
        // 这里似乎应该修改名字
        lineAndFunc[line] = funcName;
    }else if(CallInst *rightCall = dyn_cast<CallInst>(callee)){
        // 函数名是另一个call的返回值，写一个函数来处理
        // 从哪个函数获得此返回值呢？
        std::string rightName = rightCall->getCalledOperand()->getName().str();
        if(PHINode* phiName = dyn_cast<PHINode>(rightCall->getCalledOperand())){
            unsigned numIncomingValues = phiName->getNumIncomingValues();
            // 遍历所有入边值
            for (unsigned i = 0; i < numIncomingValues; ++i) {
                Value* incomingValue = phiName->getIncomingValue(i);
                std::string aNameOfPhi = incomingValue->getName().str();
                CopeWithFunRet(aNameOfPhi, M, line);
            }
        }
        else{
            CopeWithFunRet(rightName, M, line);
        }
    }
}

void Analysis(Module &M){
    for(Module::iterator fi = M.begin(), fe = M.end(); fi != fe; fi++){
        //遍历所有函数
        Function &F = *fi;
        funcName = F.getName().str();
        for(Function::iterator bi = F.begin(), be = F.end(); bi != be; bi++){
            //遍历所有块
            BasicBlock &B = *bi;
            for(BasicBlock::iterator ii = B.begin(), ie = B.end(); ii != ie; ii++){
                //遍历所有指令
                Instruction &I = *ii;
                if(CallInst *CI = dyn_cast<CallInst>(&I)){
                    // 如果是一个函数调用
                    int line = CI->getDebugLoc().getLine();    // 获取行
                    // 这个判断是去除编译器自己加的debug函数
                    if(line){
                        // 行数与所在函数名相对应
                        lineAndFunc[line] = funcName;
                        Value* callee = CI->getCalledOperand();
                        CopeWithCall(callee, M, CI, line);
                    }
                }
                else if(ReturnInst *RI = dyn_cast<ReturnInst>(&I)){
                    // 在这里处理返回
                    int line = RI->getDebugLoc().getLine();    // 获取行
                    Value *RetVal = RI->getReturnValue();
                    if(RetVal){
                        if(RetVal->getType()->isPointerTy()){
                            // 如果返回的是一个函数指针，天啊好复杂
                            CopeWithCall(RetVal, M, CI, -line);
                        }
                    }
                }
            }
        }
    }
}

void PrintResult(){
    for (const auto& pair : lineAndNames) {
        if(SHOW_RET || pair.first > 0){
            errs() << pair.first << " : ";
            bool first = true;
            for (const auto& name : pair.second) {
                if(first){
                    errs() << name;
                }
                else{
                    errs() << ", " << name;
                }
                first = false;
            }
            errs() << "\n";
        }
    }
}

///!TODO TO BE COMPLETED BY YOU FOR ASSIGNMENT 2
///Updated 11/10/2017 by fargo: make all functions
///processed by mem2reg before this pass.
struct FuncPtrPass : public ModulePass {
    static char ID; // Pass identification, replacement for typeid
    FuncPtrPass() : ModulePass(ID) {}

    bool runOnModule(Module &M) override {
        // TODO: write my code here, to print Function and Call Instructions
        while(NamesChanged){
            NamesChanged = false;
            Analysis(M);
        }
        PrintResult();
        return true;
    }
};


char FuncPtrPass::ID = 0;
static RegisterPass<FuncPtrPass> X("funcptrpass", "Print function call instruction");

static cl::opt<std::string>
InputFilename(cl::Positional,
              cl::desc("<filename>.bc"),
              cl::init(""));


int main(int argc, char **argv) {
    LLVMContext &Context = getGlobalContext();
    SMDiagnostic Err;
    // Parse the command line to read the Inputfilename
    cl::ParseCommandLineOptions(argc, argv,
                                "FuncPtrPass \n My first LLVM too which does not do much.\n");


    // Load the input module
    std::unique_ptr<Module> M = parseIRFile(InputFilename, Err, Context);
    if (!M) {
        Err.print(argv[0], errs());
        return 1;
    }

    llvm::legacy::PassManager Passes;

    ///Remove functions' optnone attribute in LLVM5.0
    Passes.add(new EnableFunctionOptPass());
    ///Transform it to SSA
    Passes.add(llvm::createPromoteMemoryToRegisterPass());

    /// Your pass to print Function and Call Instructions
    Passes.add(new FuncPtrPass());
    Passes.run(*M.get());
}

