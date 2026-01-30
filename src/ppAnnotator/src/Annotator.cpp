// Tool to track inactive (#if/#ifdef-skipped) code blocks and surface them to LLVM IR.
#include "Annotator.hpp"

#include "clang/AST/Attr.h"
#include "clang/Basic/Version.h"
#include "clang/Lex/Lexer.h"
#include "clang/Frontend/FrontendPluginRegistry.h"
#include <cstdlib>

using namespace clang;

namespace fitx::pp {

AnnotatorPPCallbacks::AnnotatorPPCallbacks(const SourceManager &SM,
                                           const LangOptions &LangOpts,
                                           std::vector<InactiveBlock> &Blocks)
    : SM(SM), LangOpts(LangOpts), Blocks(Blocks) {}

#if CLANG_VERSION_MAJOR >= 19
SourceRange AnnotatorPPCallbacks::Skipped(SourceRange Range,
                                          const Token &ConditionToken) {
  handleSkipped(Range, ConditionToken);
  return Range;
}
#else
void AnnotatorPPCallbacks::SourceRangeSkipped(SourceRange Range,
                                              SourceLocation) {
  Token Dummy;
  handleSkipped(Range, Dummy);
}
#endif

void AnnotatorPPCallbacks::handleSkipped(SourceRange Range,
                                         const Token &CondTok) {
  if (std::getenv("FITX_ANNOTATOR_DEBUG")) {
    llvm::errs() << "[annotator] skipped block\n";
  }
  InactiveBlock Block;
  Block.Range = Range;

  const SourceLocation FileBegin = SM.getFileLoc(Range.getBegin());
  const SourceLocation FileEnd = SM.getFileLoc(Range.getEnd());

  if (!SM.isWrittenInMainFile(FileBegin))
    return; // avoid rewriting system headers
  const PresumedLoc PBegin = SM.getPresumedLoc(FileBegin);
  const PresumedLoc PEnd = SM.getPresumedLoc(FileEnd);

  if (PBegin.isValid()) {
    Block.File = PBegin.getFilename();
    Block.BeginLine = PBegin.getLine();
    Block.BeginCol = PBegin.getColumn();
  }
  if (PEnd.isValid()) {
    Block.EndLine = PEnd.getLine();
    Block.EndCol = PEnd.getColumn();
  }

  // Extract the actual inactive code content
  SmallString<4096> CodeContent;
  const char *StartPtr = SM.getCharacterData(Range.getBegin());
  const char *EndPtr = SM.getCharacterData(Range.getEnd());
  if (StartPtr && EndPtr && EndPtr >= StartPtr) {
    CodeContent.append(StartPtr, EndPtr);
  }
  Block.Content = CodeContent.str().str();

  // Sanitize content for annotation
  std::string Sanitized;
  for (size_t i = 0; i < Block.Content.length() && i < 200; ++i) {
    char c = Block.Content[i];
    // Keep only safe characters for annotation
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '(' || c == ')' || c == '{' ||
        c == '}' || c == '=' || c == '+' || c == '-' || c == '*' ||
        c == '/' || c == '<' || c == '>' || c == ';' || c == ',' ||
        c == ':' || c == '_') {
      Sanitized += c;
    } else if (c == '\n' || c == '\r' || c == '\t' || c == ' ') {
      if (Sanitized.empty() || Sanitized.back() != '_') {
        Sanitized += '_';
      }
    }
  }
  if (Block.Content.length() > 200) {
    Sanitized += "...";
  }
  Block.Content = Sanitized;

  if (CondTok.is(tok::identifier) || CondTok.is(tok::kw_if) ||
      CondTok.is(tok::hash) || CondTok.is(tok::unknown) ||
      CondTok.isAnyIdentifier()) {
        if (CondTok.getLocation().isValid()) {
          SmallString<64> Spelling;
          Spelling = Lexer::getSpelling(CondTok, SM, LangOpts);
          Block.Condition = Spelling.str().str();
        }
  } else if (CondTok.is(tok::eof)) {
    Block.Condition = "<no-condition>";
  }

  Blocks.push_back(std::move(Block));
}

FunctionCollector::FunctionCollector(SourceManager &SM) : SM(SM) {}

bool FunctionCollector::VisitFunctionDecl(FunctionDecl *FD) {
  if (FD && FD->hasBody()) {
    Functions.push_back(FD);
  }
  return true;
}

const std::vector<FunctionDecl *> &FunctionCollector::getFunctions() const {
  return Functions;
}

InactiveCodeConsumer::InactiveCodeConsumer(ASTContext &Ctx, Rewriter &R,
                                           std::vector<InactiveBlock> &Blocks,
                                           bool RewriteEnabled)
    : Ctx(Ctx), SM(Ctx.getSourceManager()), RewriterRef(R), Blocks(Blocks),
      RewriteEnabled(RewriteEnabled) {}

void InactiveCodeConsumer::HandleTranslationUnit(ASTContext &Context) {
  FunctionCollector Collector(SM);
  Collector.TraverseDecl(Context.getTranslationUnitDecl());
  const auto &Funcs = Collector.getFunctions();

  unsigned MarkerIdx = 0;
  for (const auto &Block : Blocks) {
    const SourceLocation Begin = SM.getFileLoc(Block.Range.getBegin());
    const SourceLocation End = SM.getFileLoc(Block.Range.getEnd());
    const FileID BlockFile = SM.getFileID(Begin);

    FunctionDecl *Containing = nullptr;
    for (FunctionDecl *FD : Funcs) {
      if (!FD->getBody())
        continue;
      SourceRange BodyRange = FD->getBody()->getSourceRange();
      SourceLocation BodyBegin = SM.getFileLoc(BodyRange.getBegin());
      SourceLocation BodyEnd = SM.getFileLoc(BodyRange.getEnd());

      if (SM.getFileID(BodyBegin) != BlockFile)
        continue;

      const bool StartsAfterBegin =
          !SM.isBeforeInTranslationUnit(Begin, BodyBegin);
      const bool EndsBeforeEnd = !SM.isBeforeInTranslationUnit(BodyEnd, End);
      if (StartsAfterBegin && EndsBeforeEnd) {
        Containing = FD;
        break;
      }
    }

    std::string Payload;
    llvm::raw_string_ostream OS(Payload);
    OS << "inactive_block: " << Block.File << ':' << Block.BeginLine << ':'
       << Block.BeginCol << "-" << Block.EndLine << ':' << Block.EndCol;
    if (!Block.Condition.empty())
      OS << " condition=" << Block.Condition;
    // Add code content as annotation metadata
    if (!Block.Content.empty())
      OS << " code=" << Block.Content;
    OS.flush();

    if (Containing) {
      auto *Attr =
          AnnotateAttr::CreateImplicit(Ctx, Payload, nullptr, 0,
                                       SourceRange(Begin, Begin));
      Containing->addAttr(Attr);
    }

    if (!RewriteEnabled)
      continue;

    // Inject a file-scope marker so the information is preserved in IR when
    // compiling the rewritten file.
    const std::string MarkerName =
        "__clang_inactive_marker_" + std::to_string(MarkerIdx++);
    std::string Stub;
    Stub += "__attribute__((annotate(\"" + Payload + "\"), used)) ";
    Stub += "static void " + MarkerName + "(void) { }\n";
    const SourceLocation EndLoc = SM.getLocForEndOfFile(BlockFile);
    RewriterRef.InsertTextBefore(EndLoc, Stub);
  }
}

AnnotatorAction::AnnotatorAction() = default;

std::unique_ptr<ASTConsumer>
AnnotatorAction::CreateASTConsumer(CompilerInstance &CI,
                                   StringRef InFile) {
  RewriterRef.setSourceMgr(CI.getSourceManager(), CI.getLangOpts());
  return std::make_unique<InactiveCodeConsumer>(CI.getASTContext(),
                                                RewriterRef, Blocks,
                                                RewriteEnabled);
}

bool AnnotatorAction::BeginSourceFileAction(CompilerInstance &CI) {
  if (std::getenv("FITX_ANNOTATOR_DEBUG")) {
    llvm::errs() << "[annotator] BeginSourceFileAction\n";
  }
  CI.getPreprocessor().addPPCallbacks(
      std::make_unique<InactiveCodePPCallbacks>(
          CI.getSourceManager(), CI.getLangOpts(), Blocks));
  return true;
}

void AnnotatorAction::EndSourceFileAction() {
  if (!RewriteEnabled)
    return;
  RewriterRef.overwriteChangedFiles();
}

bool AnnotatorAction::ParseArgs(const CompilerInstance &,
                                const std::vector<std::string> &args) {
  if (std::getenv("FITX_ANNOTATOR_DEBUG")) {
    llvm::errs() << "[annotator] ParseArgs:";
    for (const auto &arg : args) {
      llvm::errs() << " " << arg;
    }
    llvm::errs() << "\n";
  }
  for (const auto &arg : args) {
    if (arg == "rewrite") {
      RewriteEnabled = true;
    }
  }
  return true;
}

AnnotatorAction::ActionType AnnotatorAction::getActionType() {
  return ReplaceAction;
}

} // namespace fitx::pp

static FrontendPluginRegistry::Add<fitx::pp::AnnotatorAction>
    X("annotator", "Annotate inactive preprocessor blocks");
