#pragma once

#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendPluginRegistry.h>
#include <clang/Lex/PPCallbacks.h>
#include <clang/Lex/Preprocessor.h>

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "DirEnum.h"

namespace pclower {

class PPCapturer final : public clang::PPCallbacks {
  public:
    PPCapturer(clang::SourceManager &sm,
               const clang::LangOptions &langOpts,
               std::vector<Directive> &out);

    // overrides
    void Ifdef(clang::SourceLocation loc, const clang::Token &macroNameTok,
               const clang::MacroDefinition &) override;
    void Ifndef(clang::SourceLocation loc, const clang::Token &macroNameTok,
                const clang::MacroDefinition &) override;
    void If(clang::SourceLocation loc, clang::SourceRange conditionRange,
      clang::PPCallbacks::ConditionValueKind) override;
    void Elif(clang::SourceLocation loc, clang::SourceRange conditionRange,
        clang::PPCallbacks::ConditionValueKind, clang::SourceLocation) override;
    void Else(clang::SourceLocation loc, clang::SourceLocation) override;
    void Endif(clang::SourceLocation loc, clang::SourceLocation) override;

  private:
    clang::SourceManager &sm_;
    const clang::LangOptions &langOpts_;
    std::vector<Directive> &directives_;
};

} // namespace pclower
