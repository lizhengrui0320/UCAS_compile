/************************************************************************
 *
 * @file Analysis.h
 *
 * 遍历逻辑与处理算法
 *
 ***********************************************************************/

#include <llvm/IR/Function.h>
#include <llvm/Pass.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/CFG.h>

#include <llvm/Support/raw_ostream.h>
#include <map>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/CFG.h>
#include <llvm/IR/Function.h>
#include "Dataflow.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"

int CheckType(Type* type);
void CopeWithAlloc(AllocaInst *AI, PointToSet *PTset);
void CopeWithStore(StoreInst *SI, PointToSet *PTset, int strong);
void CopeWithLoad(LoadInst *LI, PointToSet *PTset);
void CallArgs(CallInst *CI, PointToSet *PTset, DataflowResult *result);
void CopeWithCall(CallInst *CI, PointToSet *PTset, DataflowResult *result, int line);
void CopeWithCast(CastInst *CI, PointToSet *PTset);
Value* CopeWithInst(Instruction &I, PointToSet *PTset, DataflowResult *result, int strong);
Value* Analysis_Function(Function &F, DataflowResult *result, int strong);
void Analysis(Module &M, DataflowResult *result);

LLVMContext myContext;
IRBuilder<> Builder(myContext);
int Counter = 0;

int CheckType(Type* type){
    // 检查这个类型是否是函数/指针指向函数
    int i = 1;
    while(PointerType *PtrType = dyn_cast<PointerType>(type)){
        // 如果是指针
        type = PtrType->getElementType();
        i++;
    }
    if(dyn_cast<FunctionType>(type))
        return i;
    return 0;
}

Type* CheckStruct(Type *type){
    back:
    while(StructType *StructTy = dyn_cast<StructType>(type)){
        for (auto &Element : StructTy->elements()) {
            type = Element;
        }
    }
    if(PointerType *PtrType = dyn_cast<PointerType>(type)){
        type = PtrType->getElementType();
        goto back;
    }
    return type;
}

void CopeWithAlloc(AllocaInst *AI, PointToSet *PTset){
    Type *AllocatedType = AI->getAllocatedType();
    AllocatedType = CheckStruct(AllocatedType);
    if(AllocatedType->isArrayTy()){
        ArrayType* ArrayTy = dyn_cast<ArrayType>(AllocatedType);
        Type * Element = ArrayTy->getElementType();
        if(CheckType(Element)){
            Value *ConstNum = ConstantInt::get(Type::getInt32Ty(myContext), Counter++, true);
            PTset->insert(std::make_pair(AI, ConstNum));
        }
    }
    else if(CheckType(AllocatedType)){
        // 判断类型，和函数相关，则插入一个指向NULL的关系
        Value *ConstNum = ConstantInt::get(Type::getInt32Ty(myContext), Counter++, true);
        PTset->insert(std::make_pair(AI, ConstNum));
    }
}

void CopeWithStore(StoreInst *SI, PointToSet *PTset, int strong){
    // emm就很离谱，C里面写的是af_ptr = plus，但是llvm bc里面就是af_ptr指向了plus，即*af_ptr=plus
    // value存到point指向的地方 *point = value
    Value* value = SI->getValueOperand();       // 要被存的东西，如果是函数指针的话，我们要考虑一下
    Value* point = SI->getPointerOperand();     // 这里是af_ptr
    Type *ValueType = value->getType();
    Function * funcValue = dyn_cast<Function>(value);
    if(funcValue){
        // 如果value是函数名的话给加一下入口块信息
        Value * entry = &funcValue->getEntryBlock();
        if(entry && !PTset->findval(value)){
            // 我想把value->entry存入，如果没存的话就存入
            PTset->insert(std::make_pair(value, entry));
        }
    }

    ValueType = CheckStruct(ValueType);
    if(CheckType(ValueType)){
        // PTset->remove(point);
        // PTset->insert(std::make_pair(point, value));
        PTset->Store(point, value, strong);
    }
}

void CopeWithLoad(LoadInst *LI, PointToSet *PTset){
    // 把一个指针指向的东西存到寄存器中，value = *point
    // 找到point指向的所有东西
    Value* point = LI->getPointerOperand();   // 这里是af_ptr
    PTset->Load(point, LI);
}

void CallArgs(CallInst *CI, PointToSet *PTset, DataflowResult *result){
    // 找到被调用函数entry块的predPTS
    Value* callee = CI->getCalledValue();
    if(callee->getName().str().find("llvm.") == 0)
        return;
    if(callee->getName().str().find("malloc") == 0)
        return;
    Function * funcValue = dyn_cast<Function>(callee);
    if(funcValue){
        // 如果value是函数名的话给加一下入口块信息
        Value * entry = &funcValue->getEntryBlock();
        if(entry && !PTset->findval(callee)){
            // 我想把value->entry存入，如果没存的话就存入
            PTset->insert(std::make_pair(callee, entry));
        }
    }


    unsigned No = -1;
    for (Value *Arg : CI->args()) {
        No++;
        Type *ArgType = Arg->getType();
        ArgType = CheckStruct(ArgType);
        if(CheckType(ArgType)){
            // 如果是函数指针
            // 先找到被调用函数的Function及其entry块
            Value* vals[10];
            int i = PTset->retval(callee, vals);
            for(int j = 0; j < i; j++){
                Value* val = vals[j];
                // Value* val = PTset->retval(callee);
                BasicBlock *BB = dyn_cast<BasicBlock>(val);
                Function *F = BB->getParent();
                // 获取entry块的predPTS
                PointToSet *predPTS = GPTS.getPredPTS(BB);
                // 传参，将PTset中关于Arg的信息传给predPTS中
                predPTS->Merge(PTset);      // 先都给过去吧
                Value *FArg = F->getArg(No);
                // Frag = Arg，把Arg所有指向的东西都给Frag
                int num = predPTS->passArg(Arg, FArg, PTset);
                Value* ret = Analysis_Function(*F, result, num<=1);
                // PTset传递，包括处理返回值
                BasicBlock &B = *(--F->end());
                PointToSet *SuccPTS = GPTS.getSuccPTS(&B);
                PTset->Clear();
                PTset->Merge(SuccPTS);
                PTset->Assign(CI, ret); //CI = ret
            }
        }
    }
}

void CopeWithCall(CallInst *CI, PointToSet *PTset, DataflowResult *result, int line){
    Value* callee = CI->getCalledValue();
    if(Function* F = dyn_cast<Function>(callee)){
        if(F->getName().str().find("llvm.") != 0){
            std::string name = F->getName().str();
            result->addData(line, name);
        }
    }
    PTset->findname(callee, result, line);
    CallArgs(CI, PTset, result);
}

void CopeWithCast(CastInst *CI, PointToSet *PTset){
    // Type* srcType = CI->getSrcTy();
    Type* dstType = CI->getDestTy();
    dstType = CheckStruct(dstType);
    if(CheckType(dstType)){   
        Value *ConstNum = ConstantInt::get(Type::getInt32Ty(myContext), Counter++, true);
        PTset->insert(std::make_pair(CI, ConstNum));
    }
}

void CopeWithGEPI(GetElementPtrInst *GI, PointToSet *PTset){
    Value *point = GI->getPointerOperand();
    // GI = point
    PTset->Assign(GI, point);
}

Value* CopeWithInst(Instruction &I, PointToSet *PTset, DataflowResult *result, int strong){
    Value* ret = nullptr;
    if(AllocaInst *AI = dyn_cast<AllocaInst>(&I)){
        CopeWithAlloc(AI, PTset);
    }
    else if(StoreInst *SI = dyn_cast<StoreInst>(&I)){
        CopeWithStore(SI, PTset, strong);
    }
    else if(LoadInst *LI = dyn_cast<LoadInst>(&I)){
        CopeWithLoad(LI, PTset);
    }
    else if(CallInst *CI = dyn_cast<CallInst>(&I)){
        int line = CI->getDebugLoc().getLine();    // 获取行
        CopeWithCall(CI, PTset, result, line);
    }
    else if(CastInst *CI = dyn_cast<CastInst>(&I)){
        CopeWithCast(CI, PTset);
    }
    else if(GetElementPtrInst *GI = dyn_cast<GetElementPtrInst>(&I)){
        CopeWithGEPI(GI, PTset);
    }
    else if(ReturnInst *RI = dyn_cast<ReturnInst>(&I)){
        ret = RI->getReturnValue();
    }
    // I.dump();
    // PTset->dump();
    return ret;
}



Value* Analysis_Function(Function &F, DataflowResult *result, int strong){
    if(F.getName().str().find("llvm.") == 0) return nullptr;
    Value* retVal = nullptr;
    for(Function::iterator bi = F.begin(), be = F.end(); bi != be; bi++){
        // 找到入口块
        BasicBlock &B = *bi;
        if(B.getName().str() == "entry"){
            // 以入口块为基础遍历所有块
            std::set<BasicBlock*> BB_set;                    // 需要被处理的块的集合

            // 初始化准备
            BB_set.insert(&B);                               // entry块放入集合中
            if(GPTS.getPredPTS(&B) == nullptr){
                PointToSet *IpredPTS = new PointToSet;           // 块前指向集
                PointToSet *IsuccPTS = new PointToSet;           // 块后指向集
                struct extraBB *IEBB = new struct extraBB;
                *IEBB = {&B, IpredPTS, IsuccPTS};   // 做成结构体变量
                GPTS.insert(IEBB);                               // 将该结构体放入全局指向关系中
            }

            // 当还有块没处理时
            while(!BB_set.empty()){
                // 取出一个没处理的块
                BasicBlock *bb = *BB_set.begin();
                BasicBlock &B = *bb;
                BB_set.erase(BB_set.begin());
                // 找到它的所有前驱块，获取所有前驱块的succPTS，将其MEET，得到该块的predPTS
                PointToSet* predPTS = GPTS.getPredPTS(bb);
                for (auto* Pred : predecessors(&B)) {
                    PointToSet* succPTS = GPTS.getSuccPTS(Pred);
                    predPTS->Merge(succPTS);
                }
                // 利用该块的predPTS，遍历该块的指令进行指向集分析
                PointToSet tempPTS;
                tempPTS.Merge(predPTS);
                for(BasicBlock::iterator ii = B.begin(), ie = B.end(); ii != ie; ii++){
                    Instruction &I = *ii;
                    if(Value* thisret = CopeWithInst(I, &tempPTS, result, strong)) retVal = thisret;
                }
                PointToSet* succPTS = GPTS.getSuccPTS(bb);
                succPTS->Merge(&tempPTS);
                // 找到它的所有后继块，将这些块组成EBB并存入GPTS中
                for (auto* Succ : successors(&B)) {
                    if(GPTS.getPredPTS(Succ) == nullptr){
                        PointToSet *NpredPTS = new PointToSet;                      // 块前指向集
                        PointToSet *NsuccPTS = new PointToSet;                      // 块后指向集
                        struct extraBB *NEBB = new struct extraBB;                  // 块、指向集结构体
                        *NEBB = {Succ, NpredPTS, NsuccPTS};                         // 初始化
                        GPTS.insert(NEBB);                                          // 将该结构体放入全局指向关系中
                    }
                    BB_set.insert(Succ);                                        // 后继块插入
                }
            }
            break;
        }
    }
    return retVal;
}

void Analysis(Module &M, DataflowResult *result){
    for(Module::iterator fi = M.begin(), fe = M.end(); fi != fe; fi++){
        //遍历所有函数
        Function &F = *fi;
        Analysis_Function(F, result, 1);
    }
}