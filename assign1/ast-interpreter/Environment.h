//==--- tools/clang-check/ClangInterpreter.cpp - Clang Interpreter tool --------------===//
//===----------------------------------------------------------------------===//
#include <stdio.h>
#include <iostream>

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/Decl.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/Tooling.h"

using namespace clang;

class StackFrame {
   /// StackFrame maps Variable Declaration to Value
   /// Which are either integer or addresses (also represented using an Integer value)
   std::map<Decl*, uintptr_t> mVars;
   std::map<Stmt*, uintptr_t> mExprs;
   /// The current stmt
   Stmt * mPC;
   int    rt_val;
   int    valid_rt = 0;
public:
	StackFrame() : mVars(), mExprs(), mPC() {
	}

	void bindDecl(Decl* decl, uintptr_t val) {
		mVars[decl] = val;
	}    
	uintptr_t getDeclVal(Decl * decl) {
		// decl->dump();
		// std::cout << "decl:" << decl << std::endl;
		assert (mVars.find(decl) != mVars.end());
		return mVars.find(decl)->second;
	}
	void bindStmt(Stmt * stmt, uintptr_t val) {
		mExprs[stmt] = val;
	}
	uintptr_t getStmtVal(Stmt * stmt) {
		//试着在这里改一下，如果这stmt对应单纯的一个数的话
		// stmt->dump();
		if(IntegerLiteral * is_int = dyn_cast<IntegerLiteral>(stmt)){
			return is_int->getValue().getZExtValue();
		}
		else{
			assert (mExprs.find(stmt) != mExprs.end());
			return mExprs[stmt];
		}
	}

	void setPC(Stmt * stmt) {
		mPC = stmt;
	}
	Stmt * getPC() {
		return mPC;
	}

	void setRT(int val){
		rt_val = val;
		valid_rt = 1;
	}
	int  getRT(){
		valid_rt = 0;
		return rt_val;
	}
	int getValid(){
		return valid_rt;
	}
};

/// Heap maps address to a value
/*
class Heap {
public:
   int Malloc(int size) ;
   void Free (int addr) ;
   void Update(int addr, int val) ;
   int get(int addr);
};
*/

class Environment {
	std::vector<StackFrame> mStack;

	FunctionDecl * mFree;				/// Declartions to the built-in functions
	FunctionDecl * mMalloc;
	FunctionDecl * mInput;
	FunctionDecl * mOutput;

	FunctionDecl * mEntry;
	public:
	/// Get the declartions to the built-in functions
	Environment() : mStack(), mFree(NULL), mMalloc(NULL), mInput(NULL), mOutput(NULL), mEntry(NULL) {
	}


	/// Initialize the Environment
	void init(TranslationUnitDecl * unit) {
		//开栈帧
		mStack.push_back(StackFrame());
		for (TranslationUnitDecl::decl_iterator i =unit->decls_begin(), e = unit->decls_end(); i != e; ++ i) {
			// i->dump();
			if (FunctionDecl * fdecl = dyn_cast<FunctionDecl>(*i) ) {
				if (fdecl->getName().equals("FREE")) mFree = fdecl;
				else if (fdecl->getName().equals("MALLOC")) mMalloc = fdecl;
				else if (fdecl->getName().equals("GET")) mInput = fdecl;
				else if (fdecl->getName().equals("PRINT")) mOutput = fdecl;
				else if (fdecl->getName().equals("main")) mEntry = fdecl;
			}
			//外面的变量声明
			if (VarDecl * vdecl  = dyn_cast<VarDecl>(*i) ){
				if(vdecl->hasInit()){
						if(IntegerLiteral * is_int = dyn_cast<IntegerLiteral>(vdecl->getInit())){
							mStack.back().bindDecl(vdecl, is_int->getValue().getZExtValue());
						}
					}
					else{
						mStack.back().bindDecl(vdecl, 0);
				}
			}
		}
	}

	FunctionDecl * getEntry() {
		return mEntry;
	}

	void binop(BinaryOperator *bop) {
		//op的左手右手捏
		Expr * left = bop->getLHS();
		Expr * right = bop->getRHS();

		//如果这个op是赋值
		if (bop->isAssignmentOp()) {
			//这里的mStack.back()是最后一个栈帧
			uintptr_t val = mStack.back().getStmtVal(right);
			uintptr_t * addr = (uintptr_t*)val;
			if (ImplicitCastExpr * iexpr = dyn_cast<ImplicitCastExpr>(right)) {
				if(UnaryOperator * urexpr = dyn_cast<UnaryOperator>(iexpr->getSubExpr()))
					// val = addr;
					;
			}
			//如果左边是DeclRefExpr，那么找到他的Decl，然后把这个值对应给Decl
			if (DeclRefExpr * declexpr = dyn_cast<DeclRefExpr>(left)) {
				mStack.back().bindStmt(left, val);
				Decl * decl = declexpr->getFoundDecl();
				mStack.back().bindDecl(decl, val);
			} else if(ArraySubscriptExpr * arryexpr = dyn_cast<ArraySubscriptExpr>(left)){
				uintptr_t* addr = (uintptr_t*)mStack.back().getStmtVal(arryexpr);
				*addr = val;
			} else if(UnaryOperator * uexpr = dyn_cast<UnaryOperator>(left)){
				uintptr_t* addr = (uintptr_t*)mStack.back().getStmtVal(uexpr);
				*addr = val;
				// std::cout << "val: " <<val << std::endl;
			}
		}
		//如果这个op是比较
		if (bop->isComparisonOp()){
			int val_l = (int)mStack.back().getStmtVal(left);
			int val_r = (int)mStack.back().getStmtVal(right);
			if(bop->getOpcodeStr()==">"){
				mStack.back().bindStmt(bop, val_l>val_r);
			}
			if(bop->getOpcodeStr()=="<"){
				mStack.back().bindStmt(bop, val_l<val_r);
			}
			if(bop->getOpcodeStr()==">="){
				mStack.back().bindStmt(bop, val_l>=val_r);
			}
			if(bop->getOpcodeStr()=="<="){
				mStack.back().bindStmt(bop, val_l<=val_r);
			}
			if(bop->getOpcodeStr()=="=="){
				mStack.back().bindStmt(bop, val_l==val_r);
			}
		}
		//如果这个op是加减法
		if (bop->isAdditiveOp()){
			uintptr_t val_l = mStack.back().getStmtVal(left);
			uintptr_t val_r = mStack.back().getStmtVal(right);
			if(ImplicitCastExpr * impexpr = dyn_cast<ImplicitCastExpr>(left)){
				if(impexpr->getType()->isPointerType() && impexpr->getType().getTypePtr()->getPointeeType()->isIntegerType()){
					val_r = sizeof(int)*val_r;
				}
			}
			if (bop->getOpcodeStr()=="+"){
				//这里要多考虑一下，如果是指针，是另一种加法
				mStack.back().bindStmt(bop, val_l+val_r);
			}
			else{
				mStack.back().bindStmt(bop, val_l-val_r);
			}
			//这里应该不用管decl，应该只有叶子节点才可能会被decl
		}
		//如果这个op是乘除法
		if (bop->isMultiplicativeOp()){
			uintptr_t val_l = mStack.back().getStmtVal(left);
			uintptr_t val_r = mStack.back().getStmtVal(right);
			if (bop->getOpcodeStr()=="*"){
				mStack.back().bindStmt(bop, val_l*val_r);
			}
			else{
				mStack.back().bindStmt(bop, val_l/val_r);
			}
		}
	}

	void uniop(UnaryOperator * uop){
		Expr * subExpr = uop->getSubExpr();
		uintptr_t val = mStack.back().getStmtVal(subExpr);
		if(uop->getOpcode()==UO_Minus) mStack.back().bindStmt(uop, -val);
		if(uop->getOpcode()==UO_Plus)  mStack.back().bindStmt(uop, val);
		if(uop->getOpcode()==UO_Deref) mStack.back().bindStmt(uop, val);
	}

	void decl(DeclStmt * declstmt) {
		// declstmt->dump();
		for (DeclStmt::decl_iterator it = declstmt->decl_begin(), ie = declstmt->decl_end();
				it != ie; ++ it) {
			Decl * decl = *it;
			if (VarDecl * vardecl = dyn_cast<VarDecl>(decl)) {
				if(vardecl->getType()->isArrayType()){
					//TODO：处理数组声明
					ASTContext &astContext = vardecl->getASTContext();
					const ConstantArrayType *arrayType = astContext.getAsConstantArrayType(vardecl->getType());
					llvm::APInt arraySize = arrayType->getSize();
					int* arrayAddr = (int*)malloc(sizeof(int) * arraySize.getZExtValue());
					// std::cout << "malloc, int* " << arrayAddr << ", size: " << sizeof(int) * arraySize.getZExtValue() << std::endl;
					mStack.back().bindDecl(vardecl, reinterpret_cast<uintptr_t>(arrayAddr));
				}
				else if(vardecl->getType()->isPointerType()){
					//初值为空指针
					mStack.back().bindDecl(vardecl, 0);
				}
				else {
					if(vardecl->hasInit()){
						if(IntegerLiteral * is_int = dyn_cast<IntegerLiteral>(vardecl->getInit())){
							mStack.back().bindDecl(vardecl, is_int->getValue().getZExtValue());
						}
					}
					else{
						mStack.back().bindDecl(vardecl, 0);
					}
				}
			}
		}
	}
	void declref(DeclRefExpr * declref) {
		mStack.back().setPC(declref);
		if (declref->getType()->isIntegerType()) {
			Decl* decl = declref->getFoundDecl();
			uintptr_t val = mStack.back().getDeclVal(decl);
			mStack.back().bindStmt(declref, val);
		} else if (declref->getType()->isArrayType()) {
			Decl* decl = declref->getFoundDecl();
			uintptr_t * addr = (uintptr_t*)mStack.back().getDeclVal(decl);
			mStack.back().bindStmt(declref, reinterpret_cast<uintptr_t>(addr));
		} else if (declref->getType()->isPointerType()){
			Decl* decl = declref->getFoundDecl();
			uintptr_t * addr = (uintptr_t*)mStack.back().getDeclVal(decl);
			mStack.back().bindStmt(declref, reinterpret_cast<uintptr_t>(addr));
		}
	}

	void cast(CastExpr * castexpr) {
		mStack.back().setPC(castexpr);
		if (castexpr->getType()->isIntegerType()) {
			Expr * expr = castexpr->getSubExpr();
			uintptr_t val = mStack.back().getStmtVal(expr);
			mStack.back().bindStmt(castexpr, val );
		}
	}

	Stmt * call(CallExpr * callexpr) {
		mStack.back().setPC(callexpr);
		int val = 0;
		FunctionDecl * callee = callexpr->getDirectCallee();
		// FunctionDecl* funcDecl = callExpr->getDirectCallee();
		if (callee) {
			FunctionDecl* funcDef = callee->getDefinition();
			if (funcDef) {
				callee = funcDef;
			}
		}
		if (callee == mInput) {
			llvm::errs() << "Please Input an Integer Value : ";
			scanf("%d", &val);

			mStack.back().bindStmt(callexpr, val);
		} else if (callee == mOutput) {
			Expr * decl = callexpr->getArg(0);
			if(ArraySubscriptExpr * arryexpr = dyn_cast<ArraySubscriptExpr>(decl)){
				uintptr_t * addr = (uintptr_t*)mStack.back().getStmtVal(decl);
				val = *addr;
			} else {
				val = mStack.back().getStmtVal(decl);
			}
			llvm::errs() << val << "\n";
		} else if(callee == mFree){
			Expr * decl = callexpr->getArg(0);
			uintptr_t *addr = (uintptr_t *)mStack.back().getStmtVal(decl);
			free(addr);
		} else if(callee == mMalloc){
			Expr * decl = callexpr->getArg(0);
			uintptr_t size = mStack.back().getStmtVal(decl);
			void * addr = malloc(size);
			mStack.back().bindStmt(callexpr, reinterpret_cast<uintptr_t>(addr));
		} else {
			//一般的自己定义的函数
			//获取参数
			unsigned numParams = callee->getNumParams(); // 获取形参数量
			uintptr_t* paramsArray = new uintptr_t[numParams];
			for (unsigned i = 0; i < numParams; ++i) {
				Expr * arg = callexpr->getArg(i);
				uintptr_t argValue = mStack.back().getStmtVal(arg);
				paramsArray[i] = argValue;
			}

			//开栈帧
			mStack.push_back(StackFrame());
			// std::cout << "pushed stack frame\n";

			//存入参数
			for (unsigned i = 0; i < numParams; ++i) {
				ParmVarDecl *paramDecl = callee->getParamDecl(i);
				mStack.back().bindDecl(paramDecl, paramsArray[i]);
				// std::cout << "param:" << i << " value:" << paramDecl <<std::endl;
				// std::cout << "arg:" << i << " value:" << paramsArray[i] <<std::endl;
			}
			
			//跳转
			return callee->getBody();
		}
		return nullptr;
	}
	int call_return (CallExpr * callexpr) {
		int valid = mStack.back().getValid();
		if(valid){
			int return_val = mStack.back().getRT();
			//弹栈
			mStack.pop_back();
			mStack.back().bindStmt(callexpr, return_val);
		}
		return valid;
	}
	void popStack(){
		mStack.pop_back();
	}
	int ifcond(IfStmt * ifstmt){
		return mStack.back().getStmtVal(ifstmt->getCond());
	}
	int whilecond(WhileStmt * whstmt){
		return mStack.back().getStmtVal(whstmt->getCond());
	}
	int forcond(ForStmt * forstmt){
		return mStack.back().getStmtVal(forstmt->getCond());
	}
	void rtstmt(ReturnStmt * rtstmt){
		if(Stmt *retValue = rtstmt->getRetValue()){
			mStack.back().setRT(mStack.back().getStmtVal(retValue));
		}
	}
	void arryexpr(ArraySubscriptExpr* arryexpr){
		Expr * base = arryexpr->getBase();
		Expr * indx = arryexpr->getIdx();
		uintptr_t * base_addr = (uintptr_t*)mStack.back().getStmtVal(base);
		uintptr_t indx_val = mStack.back().getStmtVal(indx);
		uintptr_t * addr = base_addr + indx_val;
		mStack.back().bindStmt(arryexpr, reinterpret_cast<uintptr_t>(addr));
	}
	void icast(ImplicitCastExpr* icast){
		if(icast->getSubExpr()->getType()->isArrayType()) {
			uintptr_t * addr = (uintptr_t*)mStack.back().getStmtVal(icast->getSubExpr());
			mStack.back().bindStmt(icast, reinterpret_cast<uintptr_t>(addr));
		} else if(ArraySubscriptExpr * arryexpr = dyn_cast<ArraySubscriptExpr>(icast->getSubExpr())) {
			uintptr_t * addr = (uintptr_t*)mStack.back().getStmtVal(arryexpr);
			uintptr_t val = *addr;
			mStack.back().bindStmt(icast, reinterpret_cast<uintptr_t>(val));
		} else if(DeclRefExpr *declRefExpr = dyn_cast<DeclRefExpr>(icast->getSubExpr())){
			if (FunctionDecl *funcDecl = dyn_cast<FunctionDecl>(declRefExpr->getDecl())) {
				;
			} else {
				uintptr_t val = mStack.back().getStmtVal(icast->getSubExpr());
				mStack.back().bindStmt(icast, val);
			}
		} else if(UnaryOperator * uniexpr = dyn_cast<UnaryOperator>(icast->getSubExpr())){
			if (uniexpr->getOpcode()==UO_Deref) {
				uintptr_t val = mStack.back().getStmtVal(icast->getSubExpr());
				uintptr_t * addr = (uintptr_t*) val;
				mStack.back().bindStmt(icast, *addr);
			} else {
				uintptr_t val = mStack.back().getStmtVal(icast->getSubExpr());
				mStack.back().bindStmt(icast, val);
			}
		} else {
			uintptr_t val = mStack.back().getStmtVal(icast->getSubExpr());
			mStack.back().bindStmt(icast, val);
		}
	}
	void ccast(CStyleCastExpr* ccast){
		uintptr_t addr = mStack.back().getStmtVal(ccast->getSubExpr());
		mStack.back().bindStmt(ccast, addr);
	}
	void un_fuck(UnaryExprOrTypeTraitExpr* un_fuck){
		//deal with the fucking sizeof() function
		QualType argType = un_fuck->getArgumentType();
		int val = 0;
		if(argType.getAsString() == "int") val = sizeof(int);
		else if(argType.getAsString() == "char") val = sizeof(char);
		else val = sizeof(void *);
		mStack.back().bindStmt(un_fuck, val);
	}
	void parexp(ParenExpr* parexp){
		uintptr_t val = mStack.back().getStmtVal(parexp->getSubExpr());
		mStack.back().bindStmt(parexp, val);
	}
};


