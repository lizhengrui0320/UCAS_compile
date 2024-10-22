//==--- tools/clang-check/ClangInterpreter.cpp - Clang Interpreter tool --------------===//
//===----------------------------------------------------------------------===//

#include <iostream>
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/EvaluatedExprVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/Tooling.h"

using namespace clang;

#include "Environment.h"

class InterpreterVisitor : 
    public EvaluatedExprVisitor<InterpreterVisitor> {
    public:
    explicit InterpreterVisitor(const ASTContext &context, Environment * env)
    : EvaluatedExprVisitor(context), mEnv(env) {}
    virtual ~InterpreterVisitor() {}

    virtual void VisitBinaryOperator (BinaryOperator * bop) {
        VisitStmt(bop);
        mEnv->binop(bop);
    }
    virtual void VisitUnaryOperator (UnaryOperator * uop){
        VisitStmt(uop);
        mEnv->uniop(uop);
    }
    virtual void VisitDeclRefExpr(DeclRefExpr * expr) {
        VisitStmt(expr);
        mEnv->declref(expr);
    }
    virtual void VisitCastExpr(CastExpr * expr) {
        VisitStmt(expr);
        mEnv->cast(expr);
    }
    virtual void VisitCallExpr(CallExpr * call) {
        VisitStmt(call);
        if(Stmt * entry = mEnv->call(call)){
            // Visit(entry);
            int i = 0;
            for (auto *child : entry->children()) {
                // 处理每个子节点
                Visit(child);
                if(i=mEnv->call_return(call)) break;
            }
            //如果函数体没有return语句，那么call_return函数一直不会弹出栈帧，应该在此处手动弹栈
            if(!i) mEnv->popStack();
        }
    }
    virtual void VisitDeclStmt(DeclStmt * declstmt) {
        mEnv->decl(declstmt);
    }
    virtual void VisitIfStmt(IfStmt * ifstmt){
        //cond遍历，让他算完结果
        Visit(ifstmt->getCond());
        if(mEnv->ifcond(ifstmt)){
            auto * then_stmt = ifstmt->getThen();
            if(then_stmt) Visit(then_stmt);
        }
        else{
            auto * else_stmt = ifstmt->getElse();
            if(else_stmt) Visit(else_stmt);
        }
    }
    virtual void VisitWhileStmt(WhileStmt * whstmt){
        Visit(whstmt->getCond());
        while(mEnv->whilecond(whstmt)){
            Visit(whstmt->getBody());
            Visit(whstmt->getCond());
        }
    }
    virtual void VisitForStmt(ForStmt * forstmt){
        if(forstmt->getInit())
            Visit(forstmt->getInit());
        Visit(forstmt->getCond());
        while(mEnv->forcond(forstmt)){
            Visit(forstmt->getBody());
            Visit(forstmt->getInc());
            Visit(forstmt->getCond());
        }
    }
    virtual void VisitReturnStmt(ReturnStmt * rtstmt){
        VisitStmt(rtstmt);
        mEnv->rtstmt(rtstmt);
    }
    virtual void VisitArraySubscriptExpr(ArraySubscriptExpr* arryexpr){
        VisitStmt(arryexpr);
        mEnv->arryexpr(arryexpr);
    }
    virtual void VisitImplicitCastExpr(ImplicitCastExpr* icast){
        VisitStmt(icast);
        mEnv->icast(icast);
    }
    virtual void VisitCStyleCastExpr(CStyleCastExpr* ccast){
        VisitStmt(ccast);
        mEnv->ccast(ccast);
    }
    virtual void VisitUnaryExprOrTypeTraitExpr(UnaryExprOrTypeTraitExpr* un_fuck){
        mEnv->un_fuck(un_fuck);
    }
    virtual void VisitParenExpr(ParenExpr* parexp){
        VisitStmt(parexp);
        mEnv->parexp(parexp);
    }

    private:
    Environment * mEnv;
};

class InterpreterConsumer : public ASTConsumer {
    public:
    explicit InterpreterConsumer(const ASTContext& context) : mEnv(),
        mVisitor(context, &mEnv) {
    }
    virtual ~InterpreterConsumer() {}

    virtual void HandleTranslationUnit(clang::ASTContext &Context) {
        TranslationUnitDecl * decl = Context.getTranslationUnitDecl();
        mEnv.init(decl);

        FunctionDecl * entry = mEnv.getEntry();
        mVisitor.VisitStmt(entry->getBody());
        // std::cout << "entry pointer: " << entry << std::endl;
    }
    private:
    Environment mEnv;
    InterpreterVisitor mVisitor;
};

class InterpreterClassAction : public ASTFrontendAction {
    public: 
    virtual std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
    clang::CompilerInstance &Compiler, llvm::StringRef InFile) {
        return std::unique_ptr<clang::ASTConsumer>(
            new InterpreterConsumer(Compiler.getASTContext()));
    }
};

int main (int argc, char ** argv) {
    if (argc > 1) {
        clang::tooling::runToolOnCode(std::unique_ptr<clang::FrontendAction>(new InterpreterClassAction), argv[1]);
    }
}
