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
#include "PPCapture.h"

namespace pclower {

class PcLowerPluginAction final : public clang::PluginASTAction {
  public:
    // overrides
    bool ParseArgs(const clang::CompilerInstance &,
                   const std::vector<std::string> &args) override;
    std::unique_ptr<clang::ASTConsumer>
    CreateASTConsumer(clang::CompilerInstance &ci,
                      llvm::StringRef) override;
    void EndSourceFileAction() override;
    void EndSourceFileAction_orig();

  private:
    bool emitPc_ = true;
    bool emitLowered_ = true;
    bool overwriteLowered_ = false;
    std::string pcOut_;
    std::string loweredOut_;
    std::string loweredDir_;
    std::vector<Directive> directives_;

    static void writeFile(const std::string &path,
                          const std::vector<std::string> &lines);
    static std::string pcExpr(const std::vector<std::string> &stack);
    static std::string invert(const std::string &expr);
    static std::vector<std::string>
    renderPC(const std::vector<std::string> &lines,
             const std::map<unsigned, Directive> &directiveMap);
    static std::vector<std::string>
    renderLowered(const std::vector<std::string> &lines,
                  const std::map<unsigned, Directive> &directiveMap);
};

} // namespace pclower
