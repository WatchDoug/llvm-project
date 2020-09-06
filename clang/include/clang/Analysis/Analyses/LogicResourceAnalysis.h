#ifndef LLVM_CLANG_ANALYSIS_ANALYSES_LRA_H
#define LLVM_CLANG_ANALYSIS_ANALYSES_LRA_H

#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/AST.h"
#include "clang/AST/ASTContext.h"
#include "llvm/ADT/StringRef.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/Frontend/ASTConsumers.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Driver/Options.h"
#include "clang/Frontend/CompilerInstance.h"
#include "llvm/Support/raw_ostream.h"

namespace clang{
class CFG;
class CFGBlock;
class CompilerInstance;
class FieldDecl;

namespace {

class TargetVariable {
	public:
		TargetVariable(ValueDecl*);
		void print(llvm::raw_fd_ostream &);
		bool addCondition(Expr *);
		std::set<Expr *>& getTargetConditions();
	private:
		std::set<Expr *> _targetConds_;
		// either a non-FieldDecl global variable or a FieldDecl
		ValueDecl _decl_;
};

class TargetVariableExtractionVisitor : public RecursiveASTVisitor<TargetVariableExtractionVisitor> {
	public:
		explicit TargetVariableExtractionVisitor(ASTContext *Context) : _astContext_(Context){}
	private:
		ASTContext *_astContext_;
};


class TargetConditionVisitor : public RecursiveASTVisitor<TargetConditionVisitor> {
	public:
		explicit TargetConditionVisitor(ASTContext *Context) : _astContext_(Context), _isTargetCond_(false), _shouldVisitDRE_(false) {}
		bool VisitBinaryOperator(BinaryOperator *);
		bool VisitDeclRefExpr(DeclRefExpr *);
		bool VisitCallExpr(CallExpr *);
		bool isTargetCondition();
	private:
		ASTContext *_astContext_;
		bool _isTargetCond_;
		bool _shouldVisitDRE_;
		bool isIntComparison(const BinaryOperator *BO);
};

class LogicResourceAnalysisVisitor : public RecursiveASTVisitor<LogicResourceAnalysisVisitor> {
    public:
        explicit LogicResourceAnalysisVisitor(ASTContext *Context) : _astContext_(Context) {}
        bool VisitFunctionDecl(FunctionDecl *);
    
    private:
        ASTContext *_astContext_;
		std::set<CFGBlock*> _errorReturns_;
		std::set<CFGBlock*> _normalReturns_;
		std::set<CFGBlock*> _taskHangs_;
		std::set<CFGBlock*> _targetConds_;
        void parseCFG(CFG *);
        void collectCriticalBehavior(CFGBlock *);
        void collectTargetConds(CFGBlock *);
        void printStmt(const Stmt *, const FunctionDecl *);
};

class LogicResourceAnalysisASTConsumer : public ASTConsumer {
	public:
		explicit LogicResourceAnalysisASTConsumer(ASTContext *Context) : _visitor_(Context) {}
		virtual void HandleTranslationUnit(ASTContext &Context) {
			_visitor_.TraverseDecl(Context.getTranslationUnitDecl());
		}
	private:
		LogicResourceAnalysisVisitor _visitor_;
};

class LogicResourceAnalysisAction : public PluginASTAction {
    protected:
	std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI, StringRef file) {
		return std::make_unique<LogicResourceAnalysisASTConsumer>(&CI.getASTContext());
	}
	     
	bool ParseArgs(const CompilerInstance &CI, const std::vector<std::string> &args) override {
		return true;
	}

	PluginASTAction::ActionType getActionType() override {
		return AddAfterMainAction;
	}
};

}
}

#endif