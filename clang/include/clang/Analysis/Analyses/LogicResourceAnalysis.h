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
using AssignmentMap = std::map<ValueDecl *, ValueDecl *>;

template <bool isFlowSensitive>
class LogicResourceAssignmentMap;

template <>
class LogicResourceAssignmentMap<false>
{
	public:
		LogicResourceAssignmentMap(CFG *G) : _cfg_(G) {buildAssignmentMap();}
		AssignmentMap &getAssignmentMap(CFGBlock *);
	private:
		CFG *_cfg_;
		// Flow-insensitive implementation
		AssignmentMap _assignMap_;
		void buildAssignmentMap();
};

template <>
class LogicResourceAssignmentMap<true>
{
	public:
		LogicResourceAssignmentMap(CFG *G) : _cfg_(G) {buildAssignmentMap();}
		AssignmentMap &getAssignmentMap(CFGBlock *);
	private:
		CFG *_cfg_;
		// Flow-sensitive implementation
		std::map<CFGBlock *, AssignmentMap> _assignMap_;
		void buildAssignmentMap();
};

class ConsequenceBehaviorVisitor : public RecursiveASTVisitor<ConsequenceBehaviorVisitor> {
	public:
		explicit ConsequenceBehaviorVisitor(ASTContext *Context) : _astContext_(Context), _isCharacteristicCall_(false), _isErrReturn_(false), _isNormalReturn_(false), _traverseReturn_(false) {}
		bool dataTraverseStmtPost(Stmt *);
		bool VisitReturnStmt(ReturnStmt *);
		bool VisitCallExpr(CallExpr *);
		bool isCharacteristicCall();
		bool isErrReturn();
		bool isNormalReturn();
	private:
		ASTContext *_astContext_;
		bool _isCharacteristicCall_;
		bool _isErrReturn_;
		bool _isNormalReturn_;
		bool _traverseReturn_;
};

class PotentialLRVCollector : public RecursiveASTVisitor<PotentialLRVCollector> {
	public:
		explicit PotentialLRVCollector(ASTContext *Context) : _astContext_(Context), _containCallExpr_(false) {}
		bool VisitMemberExpr(MemberExpr *);
		bool VisitDeclRefExpr(DeclRefExpr *);
		bool VisitCallExpr(CallExpr *);
		bool containPotentialLRV();
		bool containCallExpr();
		std::set<ValueDecl *> &getPotentialLRV();
	private:
		ASTContext *_astContext_;
		std::set<ValueDecl *> _potentialLRV_;
		bool _containCallExpr_;
};

class LogicResourceAnalysisASTConsumer : public ASTConsumer {
	public:
		explicit LogicResourceAnalysisASTConsumer(ASTContext *Context) : _astContext_(Context) {}
		virtual void HandleTranslationUnit(ASTContext &) override;
	private:
		ASTContext *_astContext_;
		std::set<CFGBlock*> _errorReturns_;
		std::set<CFGBlock*> _normalReturns_;
		std::set<CFGBlock*> _characteristicCalls_;
		std::set<CFGBlock*> _criticalConds_;
		std::map<ValueDecl*, std::set<std::pair<FunctionDecl *, Stmt *> > > _LRVInfo_;
		std::set<std::pair<FunctionDecl *, Stmt *> > _undecidedConds_;
        void parseCFG(CFG *);
        void collectConsequenceBehavior(CFGBlock *);
        void collectTargetConds(CFGBlock *);
		void filterCriticalConds(CFG *);
        void printStmt(llvm::raw_fd_ostream &, const FunctionDecl *, const Stmt *);
		void printResult(llvm::raw_fd_ostream &);
		void insertLRVInfo(ValueDecl *, FunctionDecl *, Stmt *);
		void insertUndecidedConds(FunctionDecl *, Stmt *);
		bool visitFunctionDecl(FunctionDecl *);
		void clearStats();
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