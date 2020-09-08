#include "clang/Analysis/CFG.h"
#include "clang/Analysis/Analyses/Dominators.h"
#include "clang/Analysis/Analyses/LogicResourceAnalysis.h"
#include "clang/Analysis/AnalysisDeclContext.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/Frontend/FrontendPluginRegistry.h"
#include "llvm/Support/FileUtilities.h"

#include "llvm/Support/FileSystem.h"

using namespace clang;

/* -----------------------------------------
Improvment: Check semantics of error number
-------------------------------------------- */

const std::set<std::string> CriticalFunctionNames = {"schedule", "schedule_idle", "schedule_user", "schedule_preempt_disabled", "preempt_schedule", "preempt_schedule_notrace", "io_schedule_timeout", "io_schedule", "panic"};

const std::set<std::string> ErrorFunctionNames = {"ERR_PTR", "PTR_ERR"};

void LogicResourceAnalysisASTConsumer::HandleTranslationUnit(ASTContext &Context) {
	auto topdecl = Context.getTranslationUnitDecl();
	std::error_code ec;
	llvm::raw_fd_ostream fstream("./cmp-finder.txt", ec, llvm::sys::fs::OF_Text | llvm::sys::fs::OF_Append);
	for (auto iter = topdecl->decls_begin(); iter != topdecl->decls_end(); ++iter) {
		if (auto fd = iter->getAsFunction()){
			visitFunctionDecl(fd);
		}
	}
	printResult(fstream);
}

bool LogicResourceAnalysisASTConsumer::visitFunctionDecl(FunctionDecl *FD) {
	clearStats();
	AnalysisDeclContextManager acm(*_astContext_);
	AnalysisDeclContext ac(&acm, FD);
	auto cfg = ac.getCFG();
	if (cfg == nullptr){
		return true;
	}
	parseCFG(cfg);
	for (auto item : _criticalConds_) {
		Stmt *cond = item->getTerminatorCondition();
		if (cond != nullptr){
			PotentialLRVCollector plrvc(_astContext_);
			plrvc.TraverseStmt(cond);
			if (plrvc.containPotentialLRV()) {
				auto plrvset = plrvc.getPotentialLRV();
				for (auto vd : plrvset) {
					insertLRVInfo(vd, FD, cond);
				}
			}
			else {
				insertUndecidedConds(FD, cond);
			}
		}
	}
	
	return true;
}

void LogicResourceAnalysisASTConsumer::parseCFG(CFG *G) {
	for (auto blk_iter = G->begin(); blk_iter != G->end(); ++blk_iter) {
		CFGBlock *cfgblk = *blk_iter;
		collectTargetConds(cfgblk);
		collectConsequenceBehavior(cfgblk);
	}
	// filter critical conditions
	filterCriticalConds(G);
}

void LogicResourceAnalysisASTConsumer::filterCriticalConds(CFG *G) {
	std::set<CFGBlock *> dependentBlocks;
	std::set<CFGBlock *> visitedBlocks;
	std::vector<CFGBlock *> workList;
	CFGDominatorTreeImpl<false> dt(G);
	ControlDependencyCalculator cdc(G);
		for (auto item : _errorReturns_) {
		auto cdv = cdc.getControlDependencies(item);
		for (auto iter = cdv.begin(); iter != cdv.end(); ++iter) {
			if (dependentBlocks.find(*iter) == dependentBlocks.end()){
				workList.insert(workList.end(), *iter);
			}
		}
		dependentBlocks.insert(cdv.begin(), cdv.end());
	}
	for (auto item : _characteristicCalls_) {
		auto cdv = cdc.getControlDependencies(item);
		for (auto iter = cdv.begin(); iter != cdv.end(); ++iter) {
			if (dependentBlocks.find(*iter) == dependentBlocks.end()){
				workList.insert(workList.end(), *iter);
			}
		}
		dependentBlocks.insert(cdv.begin(), cdv.end());
	}
	visitedBlocks.insert(&G->getEntry());
	visitedBlocks.insert(&G->getExit());
	while (workList.size()) {
		CFGBlock *curblk = workList.back();
		workList.pop_back();
		if (visitedBlocks.find(curblk) == visitedBlocks.end()) {
			visitedBlocks.insert(curblk);
			auto cdv = cdc.getControlDependencies(curblk);
			for (auto iter = cdv.begin(); iter != cdv.end(); ++iter) {
				if (dependentBlocks.find(*iter) == dependentBlocks.end()){
					workList.insert(workList.end(), *iter);
				}
			}
			dependentBlocks.insert(cdv.begin(), cdv.end());
		}
	}
	for (auto iter = _criticalConds_.begin(); iter != _criticalConds_.end();) {
		if (dependentBlocks.find(*iter) == dependentBlocks.end()){
			iter = _criticalConds_.erase(iter);
		}
		else
			++iter;
	}
}

void LogicResourceAnalysisASTConsumer::collectTargetConds(CFGBlock *B) {
	Stmt *cond = B->getTerminatorCondition();
	if (cond != nullptr){
		PotentialLRVCollector plrvc(_astContext_);
		plrvc.TraverseStmt(cond);
		if (plrvc.containPotentialLRV() || plrvc.containCallExpr()) {
			_criticalConds_.insert(B);
		}
	}
}

void LogicResourceAnalysisASTConsumer::collectConsequenceBehavior(CFGBlock *B) {
	for (auto iter = B->begin(); iter < B->end(); ++iter) {
		auto e = iter->getAs<CFGStmt>();
		if (e) {
			auto stmt = const_cast<Stmt*>(e->getStmt());
			ConsequenceBehaviorVisitor cbv(_astContext_);
			cbv.TraverseStmt(stmt);
			if (cbv.isCharacteristicCall())
				_characteristicCalls_.insert(B);
			if (cbv.isErrReturn())
				_errorReturns_.insert(B);
			if (cbv.isNormalReturn())
				_normalReturns_.insert(B);
		}
	}
}

void LogicResourceAnalysisASTConsumer::printStmt(llvm::raw_fd_ostream &fstream, const FunctionDecl *F, const Stmt *S){
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

void LogicResourceAnalysisASTConsumer::printResult(llvm::raw_fd_ostream &fstream) {
	for (auto lrv : _LRVInfo_){
		fstream << "Identifier: ";
		lrv.first->printQualifiedName(fstream);
		fstream << "\n";
		fstream << "Related Critical Conditions:\n";
		for (auto condinfo : lrv.second) {
			printStmt(fstream, condinfo.first, condinfo.second);
		}
		fstream << "----------------------------------------\n";
	}
	fstream << "---!!!---\nUndecided Conditions:\n";
	for (auto condinfo : _undecidedConds_) {
		printStmt(fstream, condinfo.first, condinfo.second);
	}
}

void LogicResourceAnalysisASTConsumer::insertLRVInfo(ValueDecl *VD, FunctionDecl *FD, Stmt *S) {
	if (_LRVInfo_.find(VD) == _LRVInfo_.end()) {
		std::set<std::pair<FunctionDecl *, Stmt *> > s = {std::make_pair(FD, S)};
		_LRVInfo_.insert(std::make_pair(VD, s));
	}
	else {
		auto iter = _LRVInfo_.find(VD);
		iter->second.insert({FD, S});
	}
}

void LogicResourceAnalysisASTConsumer::insertUndecidedConds(FunctionDecl *FD, Stmt *S){
	_undecidedConds_.insert({FD, S});
}

void LogicResourceAnalysisASTConsumer::clearStats() {
	_errorReturns_.clear();
	_normalReturns_.clear();
	_characteristicCalls_.clear();
	_criticalConds_.clear();
}

// PotentialLRVCollector
bool PotentialLRVCollector::VisitCallExpr(CallExpr *CE) {
	if (CE->getType().getTypePtr()->isIntegerType())
		_containCallExpr_ = true;
	return true;
}

bool PotentialLRVCollector::VisitMemberExpr(MemberExpr *ME) {
	if (ME->getType().getTypePtr()->isIntegerType()){
		auto memberdecl = ME->getMemberDecl();
		if (!memberdecl)
			return true;
		if (auto fd = dyn_cast<FieldDecl>(memberdecl)){
			_potentialLRV_.insert(fd);
		}
	}
	return true;
}

bool PotentialLRVCollector::VisitDeclRefExpr(DeclRefExpr *DRE) {
	auto decl = DRE->getDecl();
	if(DRE->getType().getTypePtr()->isIntegerType()){
		if (auto vd = dyn_cast<VarDecl>(decl)) {
			if (vd->hasGlobalStorage()) {
				_potentialLRV_.insert(vd);
			}
		}
	}
	return true;
}

bool PotentialLRVCollector::containPotentialLRV() {
	 return _potentialLRV_.size() > 0;
}

bool PotentialLRVCollector::containCallExpr() {
	return _containCallExpr_;
}

std::set<ValueDecl *>& PotentialLRVCollector::getPotentialLRV() {
	return _potentialLRV_;
}



// CriticalBehaiorVisitor
bool ConsequenceBehaviorVisitor::dataTraverseStmtPost(Stmt *S){
	if (isa<ReturnStmt>(S))
		_traverseReturn_ = false;
	return true;
}

bool ConsequenceBehaviorVisitor::VisitReturnStmt(ReturnStmt *RS) {
	_traverseReturn_ = true;
	_isErrReturn_ = false;
	_isNormalReturn_ = true;
	// check returned value;
	Expr *retval = RS->getRetValue();
	if (retval == nullptr)
		return true;
	while (auto castexpr = dyn_cast<CastExpr>(retval)) {
		retval = castexpr->getSubExpr();
	}
	if (auto uo = dyn_cast<UnaryOperator>(retval)){
		if (uo->getOpcode() != UO_Minus)
			return true;
		retval = uo->getSubExpr();
	}
	if (auto intliteral = dyn_cast<IntegerLiteral>(retval)) {
		int64_t intval = intliteral->getValue().getZExtValue();
		if (0 < intval && intval < 4096 && intval != 1 && intval != 13) {
			_isErrReturn_ = true;
			_isNormalReturn_ = false;
		}
	}
	return true;
}

bool ConsequenceBehaviorVisitor::VisitCallExpr(CallExpr *CE) {
	Expr *callee = CE->getCallee();
	while (auto castexpr = dyn_cast<CastExpr>(callee)) {
		callee = castexpr->getSubExpr();
	}
	if (auto dre = dyn_cast<DeclRefExpr>(callee)) {
		auto decl = dre->getDecl();
		// We do not consider indirect calls
		if (auto funcdecl = dyn_cast<FunctionDecl>(decl)) {
			auto fname = funcdecl->getNameInfo().getName().getAsString();
			if (CriticalFunctionNames.find(fname) != CriticalFunctionNames.end()) {
				_isCharacteristicCall_ = true;
			}
			if (_traverseReturn_ && ErrorFunctionNames.find(fname) != ErrorFunctionNames.end()) {
				_isErrReturn_ = true;
				_isNormalReturn_ = false;
			}
		}
	}
	return true;
}

bool ConsequenceBehaviorVisitor::isCharacteristicCall() {
	return _isCharacteristicCall_;
}

bool ConsequenceBehaviorVisitor::isErrReturn() {
	return _isErrReturn_;
}

bool ConsequenceBehaviorVisitor::isNormalReturn() {
	return _isNormalReturn_;
}

AssignmentMap& LogicResourceAssignmentMap<false>::getAssignmentMap(CFGBlock *B){
	return _assignMap_;
}

void LogicResourceAssignmentMap<false>::buildAssignmentMap(){

}

static FrontendPluginRegistry::Add<LogicResourceAnalysisAction>
X("logic-resource-analysis", "Find possible logic resource in kernel");

// bool IntComparisonVisitor::isIntComparison(const BinaryOperator *BO) {
// 	auto op = BO->getOpcode();
// 	// || op == BO_EQ || op == BO_NE
// 	if (op == BO_LT || op == BO_GT || op == BO_LE || op == BO_GE) {
// 		auto l = BO->getLHS()->getType();
// 		auto r = BO->getRHS()->getType();
// 		if (l->isIntegerType() || r->isIntegerType())
// 			return true;
// 	}
// 	return false;
// }

