#include "clang/Analysis/CFG.h"
#include "clang/Analysis/Analyses/Dominators.h"
#include "clang/Analysis/Analyses/LogicResourceAnalysis.h"
#include "clang/Analysis/AnalysisDeclContext.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/Frontend/FrontendPluginRegistry.h"
#include "llvm/Support/FileUtilities.h"
#include "llvm/Support/raw_ostream.h"
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
	return true;
}

bool LogicResourceAnalysisVisitor::isTargetCondition(const Expr *E){
	return false;
}

void LogicResourceAnalysisVisitor::parseCFG(CFG *G) {
	for (auto blk_iter = G->begin(); blk_iter != G->end(); ++blk_iter){
		CFGBlock *cfgblk = *blk_iter;
		
		const Expr *cond = cfgblk->getLastCondition();
		if (cond != nullptr) {
			collectTargetConds(cond);
		}
		collectCriticalBehavior(cfgblk);
		cfgblk->dump();
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
				// handle calling to hanging functions here
			}
		}
	}
}

void LogicResourceAnalysisVisitor::collectTargetConds(const Expr *E) {
}

bool LogicResourceAnalysisVisitor::isIntComparison(const BinaryOperator *BO) {
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


static FrontendPluginRegistry::Add<LogicResourceAnalysisAction>
X("logic-resource-analysis", "Find possible logic resource in kernel");
