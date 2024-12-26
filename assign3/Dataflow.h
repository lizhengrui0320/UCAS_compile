/************************************************************************
 *
 * @file Dataflow.h
 *
 * 相关数据结构定义
 *
 ***********************************************************************/

#ifndef _DATAFLOW_H_
#define _DATAFLOW_H_

#include <llvm/Support/raw_ostream.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/IRBuilder.h>
#include <map>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/CFG.h>
#include <llvm/IR/Function.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/IRReader/IRReader.h>
#define DEBUG_OUTPUT 0

using namespace llvm;

void dumpValue(Value* Val){
    if(Val != nullptr){
        if(Val->hasName()){
            errs() << Val->getName().str();
        }
        else{
            errs() << "no name";
        }
    }
    else{
        errs() << "NULL";
    }
}

class DataflowResult {
private:
    std::map<int, std::vector<std::string>> data;    //行和名字的对应

public:
    // 构造函数
    DataflowResult() = default;

    // 添加数据到map中（自动降重）
    void addData(int line, const std::string& name) {
        auto it = data.find(line);
        if (it == data.end()) {
            data[line] = {name}; // 在vector中存储name
        } else {
            auto& vec = it->second; // 获取对应的vector
            bool nameExists = false;
            for (const auto& existingName : vec) {
                if (existingName == name) {
                    nameExists = true;
                    break;
                }
            }
            if (!nameExists) {
                vec.push_back(name);
            }
        }
    }

    // 打印所有数据
    void printData() const {
        for (const auto& pair : data) {
            errs() << pair.first << " : ";
            int first = 1;
            for (const auto& name : pair.second) {
                if(!first) errs() << ", ";
                first = 0;
                errs() << name;
            }
            errs() << "\n";
        }
    }
};

class PointToSet {
private:
    int NumOfFunc = 0;
public:
    std::set<std::pair<Value*, Value*>> Pairs;             /// 对的集合，对是存储的指向信息
    PointToSet() : Pairs() {}
    PointToSet(const PointToSet & set) : Pairs(set.Pairs) {}
    
    bool operator == (const PointToSet & set) const {
        return Pairs == set.Pairs;
    }

    void dump(){
        // 打印目前指向集信息，debug用的
        if(1){
            errs() << "-------PTset dumping---------\n";
            for(const auto& pair: Pairs){
                errs() << "      ";
                dumpValue(pair.first);
                errs() << "\t->\t";
                dumpValue(pair.second);
                errs() << "\t\t|  addr:";
                errs() << pair.first;
                errs() << " -> ";
                errs() << pair.second;
                errs() << "\n";
            }
            errs() << "-------PTset dumped----------\n";
        }
    }

    int findval(Value* val){
        // 找前一项是val的对
        int count = 0;
        for(const auto& pair: Pairs){
            if(pair.first == val){
                count++;
            }
        }
        return count;
    }

    int retval(Value* val, Value* vals[]){
        // 找前一项是val的对
        int i = 0;
        for(const auto& pair: Pairs){
            if(pair.first == val){
                vals[i++] = pair.second;
            }
        }
        return i;
    }

    void findname(Value* val, DataflowResult *result, int line){
        // 找到BB对应的func名字然后返回它
        // 找到左边和val匹配的对
        for(const auto& pair: Pairs){
            if(pair.first == val){
                // 找到了，去找和这个pair一样second的pair
                for(const auto& ppair: Pairs){
                    if(pair.second == ppair.second){
                        if(Function* F = dyn_cast<Function>(ppair.first)){
                            // 太好了是名字我们有救了
                            std::string name = F->getName().str();
                            result->addData(line, name);
                        }
                    }
                }
            }
        }
    }

    void Load(Value* point, Value* value){
        // value = *point
        // 先找到所有左为value的对
        for(const auto& pair: Pairs){
            if(pair.first == point){
                // 这里的pair，左为value
                for(const auto& ppair: Pairs){
                    // 找左为pair.second的对
                    if(ppair.first == pair.second){
                        // value->ppair.second
                        Pairs.insert(std::make_pair(value, ppair.second));
                    }
                }
            }
        }
    }

    void remove(Value* val) {
        // 找到所有前面是val的指向关系，并删除
        for (auto it=Pairs.begin(); it!=Pairs.end();){
            if(it->first == val){
                it = Pairs.erase(it);
            } else {
                ++it;
            }
        }
    }

    void insert(std::pair<Value*, Value*> pair){
        Pairs.insert(pair);
        // this->dump(errs());
    }

    void Merge(PointToSet* Set){
        for(auto pair: Set->Pairs){
            Pairs.insert(pair);
        }
    }

    int passArg(Value *Arg, Value *Farg, PointToSet* PTset){
        int n = 0;
        for(auto pair: PTset->Pairs){
            if(pair.first == Arg){
                Pairs.insert(std::make_pair(Farg, pair.second));
                n++;
            }
        }
        return n;
    }

    void Store(Value *point, Value *value, int strong){
        const std::pair<Value*, Value*>* ToDelete[200];
        std::pair<Value*, Value*> ToAdd[200];
        int i = 0;
        for(auto pair: Pairs){
            if(pair.first == point){
                for(const auto& ppair: Pairs){
                    if(ppair.second == pair.second){
                        ToDelete[i] = &ppair;
                        ToAdd[i] = std::make_pair(ppair.first, value);
                        i++;
                    }
                }
            }
        }
        for(int j = 0; j < i; j++){
            if(strong)
                Pairs.erase(*ToDelete[j]);
            Pairs.insert(ToAdd[j]);
        }
    }

    void Assign(Value* a, Value*b){
        //a=b
        for(auto pair: Pairs){
            if(pair.first == b){
                Pairs.insert(std::make_pair(a, pair.second));
            }
        }
    }

    void Clear(){
        Pairs.clear();
    }
};

struct extraBB {
    BasicBlock* BB = nullptr;
    PointToSet* PredPTS = nullptr;
    PointToSet* SuccPTS = nullptr;
};

// it means global point-to sets
class globalPTS{
private:
    std::vector<extraBB*> EBB_set;
public:
    globalPTS() : EBB_set() {}

    bool operator == (const globalPTS & set) const {
        return EBB_set == set.EBB_set;
    }

    PointToSet* getPredPTS(BasicBlock* BB){
        for(auto * EBB: EBB_set){
            if(EBB->BB == BB) return EBB->PredPTS;
        }
        return nullptr;
    }

    PointToSet* getSuccPTS(BasicBlock* BB){
        for(auto * EBB: EBB_set){
            if(EBB->BB == BB) return EBB->SuccPTS;
        }
        return nullptr;
    }
    
    void insert(extraBB* EBB){
        EBB_set.push_back(EBB);
    }
};

globalPTS GPTS;

// class 

#endif /* !_DATAFLOW_H_ */
