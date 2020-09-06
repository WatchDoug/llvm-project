#include "clang/Analysis/CFG.h"
#include "clang/Analysis/Analyses/Dominators.h"
#include "clang/Analysis/Analyses/LogicResourceAnalysis.h"
#include "clang/Analysis/AnalysisDeclContext.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/Frontend/FrontendPluginRegistry.h"
#include "llvm/Support/FileUtilities.h"

#include "llvm/Support/FileSystem.h"

using namespace clang;

bool LogicResourceAnalysisVisitor::VisitFunctionDecl(FunctionDecl *FD) {
	AnalysisDeclContextManager acm(*_astContext_);
	AnalysisDeclContext ac(&acm, FD);
	auto cfg = ac.getCFG();
	if (cfg == nullptr){
		return true;
	}
	CFGDominatorTreeImpl<false> dt(cfg);
	CFGDominatorTreeImpl<true> pdt(cfg);
	parseCFG(cfg);

	//
	for (auto item : _targetConds_) {
		item->dump();
	}
	//
	return true;
}

void LogicResourceAnalysisVisitor::parseCFG(CFG *G) {
	for (auto blk_iter = G->begin(); blk_iter != G->end(); ++blk_iter){
		CFGBlock *cfgblk = *blk_iter;
		collectTargetConds(cfgblk);
		collectCriticalBehavior(cfgblk);
	}
}

void LogicResourceAnalysisVisitor::collectTargetConds(CFGBlock *B) {
	Expr *cond = dyn_cast<Expr>(B->getTerminatorCondition());
	if (cond != nullptr){
		TargetConditionVisitor tcv(_astContext_);
		tcv.TraverseStmt(cond);
		if (tcv.isTargetCondition()) {
			_targetConds_.insert(B);
		}
	}
}

void LogicResourceAnalysisVisitor::collectCriticalBehavior(CFGBlock *B) {
	for (auto iter = B->begin(); iter < B->end(); ++iter) {
		auto e = iter->getAs<CFGStmt>();
		if (e) {
			auto stmt = e->getStmt();
			if (auto retstmt = dyn_cast<ReturnStmt>(stmt)) {
			}
			else{
				// handle callexprs to hanging functions here
			}
		}
	}
}

void LogicResourceAnalysisVisitor::printStmt(const Stmt *S, const FunctionDecl *F){
	std::error_code ec;
	llvm::raw_fd_ostream fstream("./cmp-finder.txt", ec, llvm::sys::fs::OF_Text | llvm::sys::fs::OF_Append);
	S->printPretty(fstream, nullptr, PrintingPolicy(_astContext_->getLangOpts()));
	fstream << "\tin\t";
	if (F)
		fstream << F->getNameInfo().getName().getAsString();
	else {
		fstream << "???";
	}
	fstream << "\t@\t";
	S->getBeginLoc().print(fstream, _astContext_->getSourceManager());
	fstream << "\n---###---\n";
}

bool TargetConditionVisitor::VisitBinaryOperator(BinaryOperator *BO) {
	if (isIntComparison(BO))
		_shouldVisitDRE_ = true;
	else
		_shouldVisitDRE_ = false;
	return true;
}

bool TargetConditionVisitor::VisitCallExpr(CallExpr *CE) {
	if (CE->getType().getTypePtr()->isIntegerType())
		_isTargetCond_ = true;
	return true;
}

bool TargetConditionVisitor::VisitDeclRefExpr(DeclRefExpr *DRE) {
	if (!_shouldVisitDRE_)
		return 0;
	auto decl = DRE->getDecl();
	if (auto vardecl = dyn_cast<VarDecl>(decl)) {
		auto ty = vardecl->getType();
		while (auto pty = dyn_cast<PointerType>(ty)) {
			ty = pty->getPointeeType();
		}
		if (!vardecl->hasGlobalStorage() && !ty->isRecordType()) {
			return true;
		}
		_isTargetCond_ = true;
	}
	return true;
}

bool TargetConditionVisitor::isTargetCondition() {
	return _isTargetCond_;
}

bool TargetConditionVisitor::isIntComparison(const BinaryOperator *BO) {
	auto op = BO->getOpcode();
	// || op == BO_EQ || op == BO_NE
	if (op == BO_LT || op == BO_GT || op == BO_LE || op == BO_GE) {
		auto l = BO->getLHS()->getType();
		auto r = BO->getRHS()->getType();
		if (l->isIntegerType() || r->isIntegerType())
			return true;
	}
	return false;
}


static FrontendPluginRegistry::Add<LogicResourceAnalysisAction>
X("logic-resource-analysis", "Find possible logic resource in kernel");
