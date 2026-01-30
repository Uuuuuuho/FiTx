#pragma once

#include <string>
#include <vector>

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/Version.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Frontend/FrontendPluginRegistry.h"
#include "clang/Lex/PPCallbacks.h"
#include "clang/Lex/Token.h"
#include "clang/Rewrite/Core/Rewriter.h"

namespace fitx::pp {

struct InactiveBlock {
  clang::SourceRange Range;
  std::string Condition;
  std::string File;
  std::string Content;
  unsigned BeginLine = 0;
  unsigned BeginCol = 0;
  unsigned EndLine = 0;
  unsigned EndCol = 0;
};

class AnnotatorPPCallbacks : public clang::PPCallbacks {
public:
  AnnotatorPPCallbacks(const clang::SourceManager &SM,
                       const clang::LangOptions &LangOpts,
                       std::vector<InactiveBlock> &Blocks);

#if CLANG_VERSION_MAJOR >= 19
  clang::SourceRange Skipped(clang::SourceRange Range,
                             const clang::Token &ConditionToken) override;
#else
  void SourceRangeSkipped(clang::SourceRange Range,
                          clang::SourceLocation) override;
#endif

private:
  const clang::SourceManager &SM;
  const clang::LangOptions &LangOpts;
  std::vector<InactiveBlock> &Blocks;

  void handleSkipped(clang::SourceRange Range, const clang::Token &CondTok);
};

class FunctionCollector
    : public clang::RecursiveASTVisitor<FunctionCollector> {
public:
  explicit FunctionCollector(clang::SourceManager &SM);

  bool VisitFunctionDecl(clang::FunctionDecl *FD);
  const std::vector<clang::FunctionDecl *> &getFunctions() const;

private:
  clang::SourceManager &SM;
  std::vector<clang::FunctionDecl *> Functions;
};

class InactiveCodeConsumer : public clang::ASTConsumer {
public:
  InactiveCodeConsumer(clang::ASTContext &Ctx, clang::Rewriter &R,
                       std::vector<InactiveBlock> &Blocks,
                       bool RewriteEnabled);

  void HandleTranslationUnit(clang::ASTContext &Context) override;

private:
  clang::ASTContext &Ctx;
  clang::SourceManager &SM;
  clang::Rewriter &RewriterRef;
  std::vector<InactiveBlock> &Blocks;
  bool RewriteEnabled = false;
};

class AnnotatorAction : public clang::PluginASTAction {
public:
  AnnotatorAction();

  std::unique_ptr<clang::ASTConsumer>
  CreateASTConsumer(clang::CompilerInstance &CI,
                    clang::StringRef InFile) override;

  bool BeginSourceFileAction(clang::CompilerInstance &CI) override;
  void EndSourceFileAction() override;
  bool ParseArgs(const clang::CompilerInstance &CI,
                 const std::vector<std::string> &args) override;
  ActionType getActionType() override;

private:
  clang::Rewriter RewriterRef;
  std::vector<InactiveBlock> Blocks;
  bool RewriteEnabled = false;
};

// Keep compatibility with older names referenced in the file.
using InactiveCodePPCallbacks = AnnotatorPPCallbacks;
using InactiveCodeAction = AnnotatorAction;

} // namespace fitx::pp
