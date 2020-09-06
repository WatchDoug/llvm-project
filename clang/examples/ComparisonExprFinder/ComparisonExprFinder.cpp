#include "clang/Analysis/CFG.h"
#include "clang/Analysis/Analyses/Dominators.h"
#include "clang/Analysis/AnalysisDeclContext.h"
#include "clang/Driver/Options.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/AST.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/ASTConsumers.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendPluginRegistry.h"
#include "llvm/Support/FileUtilities.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/FileSystem.h"

using namespace clang;
namespace{
class ComparisonExprFinderVisitor : public RecursiveASTVisitor<ComparisonExprFinderVisitor> {
	public:
		explicit ComparisonExprFinderVisitor(ASTContext *Context) : _astContext_(Context) {}
		bool VisitFunctionDecl(FunctionDecl *FD) {
			auto ac = new AnalysisDeclContext(new AnalysisDeclContextManager(*_astContext_), FD);
			auto cfg = ac->getCFG();
			auto dt = new CFGDominatorTreeImpl<false>(cfg);
			return true;
		}
		bool VisitDeclRefExpr(DeclRefExpr *DRE) {
			auto decl = DRE->getDecl();
			if (auto vardecl = dyn_cast<VarDecl>(decl)) {
				auto ty = vardecl->getType();
				while (auto pty = dyn_cast<PointerType>(ty)) {
					ty = pty->getPointeeType();
				}
				if (!vardecl->hasGlobalStorage() && !ty->isRecordType()) {
					return true;
				}
			}
			auto bo = findTopBOParent(DRE);
			if (bo == nullptr)
				return true;
			auto ifstmt = findBottomIfStmt(bo);
			if (ifstmt == nullptr || _ifStmtFound_.find(ifstmt) != _ifStmtFound_.end())
				return true;
			_ifStmtFound_.insert(ifstmt);
			auto fd = findFunctionDecl(ifstmt);
			printStmt(ifstmt, fd);
			return true;
		}
		
	private:
		ASTContext *_astContext_;
		std::set<const Stmt*> _ifStmtFound_;
		bool isIntComparison(const BinaryOperator *BO) {
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
		const BinaryOperator *findTopBOParent(const Expr *E){
			const BinaryOperator *bo = nullptr;
			auto parents = _astContext_->getParents(*E);
			while (!parents.empty() && parents[0].template get<Expr>() != nullptr) {
				const auto *curexpr = parents[0].template get<Expr>();
				const auto *tbo = dyn_cast<BinaryOperator>(curexpr);
				if (tbo != nullptr && isIntComparison(tbo)) {
					bo = tbo;
				}
				parents = _astContext_->getParents(*curexpr);
			}
			return bo;
		}

		const IfStmt *findBottomIfStmt(const Expr *E){
			auto parents = _astContext_->getParents(*E);
			while (!parents.empty() && parents[0].template get<Expr>() != nullptr) {
				const auto *expr = parents[0].template get<Expr>();
				parents = _astContext_->getParents(*expr);
			}
			if (const auto *ifstmt = dyn_cast<IfStmt>(parents[0].template get<Stmt>()))
				return ifstmt;
			return nullptr;
		}

		const FunctionDecl *findFunctionDecl(const Stmt *S){
			auto parents = _astContext_->getParents(*S);
			while (!parents.empty()) {
				const auto *stmt = parents[0].template get<Stmt>();
				const auto *decl = parents[0].template get<Decl>();
				if (stmt)
					parents = _astContext_->getParents(*stmt);
				else if (decl) {
					parents = _astContext_->getParents(*decl);
					if (const auto *fd = dyn_cast<FunctionDecl>(decl)) {
						return fd;
					}
				}
				else
					return nullptr;
			}
			return nullptr;
		}
		void printStmt(const Stmt *S, const FunctionDecl *F){
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
};


class ComparisonExprFinderASTConsumer : public clang::ASTConsumer {
	public:
		explicit ComparisonExprFinderASTConsumer(ASTContext *Context) : _visitor_(Context) {}
		virtual void HandleTranslationUnit(ASTContext &Context) {
			_visitor_.TraverseDecl(Context.getTranslationUnitDecl());
		}
	private:
		ComparisonExprFinderVisitor _visitor_;
};


class ComparisonExprFinderAction : public clang::PluginASTAction {
	protected:
		std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI, StringRef file) {
			return std::make_unique<ComparisonExprFinderASTConsumer>(&CI.getASTContext());
		}
		     
		bool ParseArgs(const CompilerInstance &CI, const std::vector<std::string> &args) override {
			return true;
		}

		PluginASTAction::ActionType getActionType() override {
			return AddAfterMainAction;
		}
};

}

static FrontendPluginRegistry::Add<ComparisonExprFinderAction>
X("cmp-finder", "Find comparison expressions that contain global or struct member integer variables.");
