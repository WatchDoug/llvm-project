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

namespace clang{
class CFG;
class CFGBlock;
class CompilerInstance;

namespace {

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
        bool isTargetCondition(const Expr *);
        void parseCFG(CFG *);
        void collectCriticalBehavior(CFGBlock *);
        void collectTargetConds(const Expr *);
        bool isIntComparison(const BinaryOperator *);
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