#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Lex/PPCallbacks.h>
#include <clang/Lex/Preprocessor.h>
#include <clang/Tooling/CommonOptionsParser.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/raw_ostream.h>

#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace clang;
using namespace clang::tooling;

namespace {

enum class DirKind { Ifdef, Ifndef, Else, Endif };

struct Directive {
    DirKind kind;
    std::string macro;
    unsigned line;
};

class PPCapturer final : public PPCallbacks {
  public:
    PPCapturer(SourceManager &sm, std::vector<Directive> &out)
        : sm_(sm), directives_(out) {}

    void Ifdef(SourceLocation loc, const Token &macroNameTok,
               const MacroDefinition &) override {
        if (!sm_.isWrittenInMainFile(loc)) {
            return;
        }
        directives_.push_back({DirKind::Ifdef,
                               macroNameTok.getIdentifierInfo()->getName().str(),
                               sm_.getSpellingLineNumber(loc)});
    }

    void Ifndef(SourceLocation loc, const Token &macroNameTok,
                const MacroDefinition &) override {
        if (!sm_.isWrittenInMainFile(loc)) {
            return;
        }
        directives_.push_back({DirKind::Ifndef,
                               macroNameTok.getIdentifierInfo()->getName().str(),
                               sm_.getSpellingLineNumber(loc)});
    }

    void Else(SourceLocation loc, SourceLocation) override {
        if (!sm_.isWrittenInMainFile(loc)) {
            return;
        }
        directives_.push_back({DirKind::Else, "", sm_.getSpellingLineNumber(loc)});
    }

    void Endif(SourceLocation loc, SourceLocation) override {
        if (!sm_.isWrittenInMainFile(loc)) {
            return;
        }
        directives_.push_back({DirKind::Endif, "", sm_.getSpellingLineNumber(loc)});
    }

  private:
    SourceManager &sm_;
    std::vector<Directive> &directives_;
};

class PCAction final : public ASTFrontendAction {
  public:
    explicit PCAction(const std::string &outputPath) : outputPath_(outputPath) {}

    std::unique_ptr<ASTConsumer>
    CreateASTConsumer(CompilerInstance &, llvm::StringRef) override {
        return std::make_unique<ASTConsumer>();
    }

    bool BeginSourceFileAction(CompilerInstance &ci) override {
        auto &pp = ci.getPreprocessor();
        pp.addPPCallbacks(std::make_unique<PPCapturer>(ci.getSourceManager(), directives_));
        return true;
    }

    void EndSourceFileAction() override {
        SourceManager &sm = getCompilerInstance().getSourceManager();
        FileID mainFile = sm.getMainFileID();
        auto buffer = sm.getBufferData(mainFile);

        std::vector<std::string> lines;
        std::stringstream ss(std::string(buffer.begin(), buffer.end()));
        std::string line;
        while (std::getline(ss, line)) {
            lines.push_back(line + "\n");
        }

        std::map<unsigned, Directive> directiveMap;
        for (const auto &dir : directives_) {
            directiveMap[dir.line] = dir;
        }

        std::vector<std::string> out;
        std::vector<std::string> pcStack;

        for (size_t i = 0; i < lines.size(); ++i) {
            unsigned lineNo = static_cast<unsigned>(i + 1);
            auto it = directiveMap.find(lineNo);
            if (it != directiveMap.end()) {
                const auto &dir = it->second;
                if (dir.kind == DirKind::Ifdef) {
                    pcStack.push_back(dir.macro);
                    out.push_back("// PC-BEGIN: " + pcExpr(pcStack) + "\n");
                } else if (dir.kind == DirKind::Ifndef) {
                    pcStack.push_back("!" + dir.macro);
                    out.push_back("// PC-BEGIN: " + pcExpr(pcStack) + "\n");
                } else if (dir.kind == DirKind::Else) {
                    if (!pcStack.empty()) {
                        pcStack.back() = invert(pcStack.back());
                    }
                    out.push_back("// PC-ELSE: " + pcExpr(pcStack) + "\n");
                } else if (dir.kind == DirKind::Endif) {
                    if (!pcStack.empty()) {
                        pcStack.pop_back();
                    }
                    out.push_back("// PC-END: " + pcExpr(pcStack) + "\n");
                }
                continue;
            }

            std::string currentPc = pcExpr(pcStack);
            if (currentPc != "TRUE") {
                out.push_back("// PC: " + currentPc + "\n");
            }
            out.push_back(lines[i]);
        }

        std::error_code ec;
        llvm::raw_fd_ostream outFile(outputPath_, ec);
        if (ec) {
            llvm::errs() << "Failed to write output: " << ec.message() << "\n";
            return;
        }
        for (const auto &l : out) {
            outFile << l;
        }
    }

  private:
    std::string outputPath_;
    std::vector<Directive> directives_;

    static std::string pcExpr(const std::vector<std::string> &stack) {
        if (stack.empty()) {
            return "TRUE";
        }
        std::string out;
        for (size_t i = 0; i < stack.size(); ++i) {
            if (i > 0) {
                out += " && ";
            }
            out += stack[i];
        }
        return out;
    }

    static std::string invert(const std::string &expr) {
        if (!expr.empty() && expr[0] == '!') {
            return expr.substr(1);
        }
        return "!" + expr;
    }
};

} // namespace

static llvm::cl::OptionCategory ToolCategory("pc-propagation-clang");
static llvm::cl::opt<std::string> OutputPath(
    "output", llvm::cl::desc("Output annotated C file"), llvm::cl::value_desc("path"),
    llvm::cl::Required, llvm::cl::cat(ToolCategory));

int main(int argc, const char **argv) {
    auto expectedParser = CommonOptionsParser::create(argc, argv, ToolCategory);
    if (!expectedParser) {
        llvm::errs() << expectedParser.takeError();
        return 1;
    }

    CommonOptionsParser &optionsParser = expectedParser.get();
    auto sources = optionsParser.getSourcePathList();
    if (sources.size() != 1) {
        llvm::errs() << "Expected a single input source file\n";
        return 1;
    }

    std::vector<std::string> args = {"-x", "c", "-std=c11"};
    FixedCompilationDatabase compDb(".", args);
    ClangTool tool(compDb, sources);

    class PCActionFactory : public FrontendActionFactory {
      public:
        explicit PCActionFactory(std::string outputPath)
            : outputPath_(std::move(outputPath)) {}

        std::unique_ptr<FrontendAction> create() override {
            return std::make_unique<PCAction>(outputPath_);
        }

      private:
        std::string outputPath_;
    };

    auto factory = std::make_unique<PCActionFactory>(OutputPath);
    return tool.run(factory.get());
}
