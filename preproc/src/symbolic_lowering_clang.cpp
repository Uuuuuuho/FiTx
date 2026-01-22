#include <clang/AST/ASTConsumer.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Lex/PPCallbacks.h>
#include <clang/Lex/Preprocessor.h>
#include <clang/Tooling/CommonOptionsParser.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/Path.h>
#include <llvm/Support/raw_ostream.h>

#include <cctype>
#include <map>
#include <optional>
#include <regex>
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

struct MacroGuard {
    std::string macro;
    bool negated = false;
    std::string name;
    std::string params;
    std::string ifBody;
    std::string elseBody;
    bool inElse = false;
};

class LoweringPPCallbacks final : public PPCallbacks {
  public:
    LoweringPPCallbacks(SourceManager &sm, std::vector<Directive> &out)
        : sm_(sm), directives_(out) {}

    void Ifdef(SourceLocation loc, const Token &macroNameTok,
               const MacroDefinition &) override {
        if (!sm_.isWrittenInMainFile(loc)) {
            return;
        }
        directives_.push_back(
            {DirKind::Ifdef, macroNameTok.getIdentifierInfo()->getName().str(),
             sm_.getSpellingLineNumber(loc)});
    }

    void Ifndef(SourceLocation loc, const Token &macroNameTok,
                const MacroDefinition &) override {
        if (!sm_.isWrittenInMainFile(loc)) {
            return;
        }
        directives_.push_back(
            {DirKind::Ifndef, macroNameTok.getIdentifierInfo()->getName().str(),
             sm_.getSpellingLineNumber(loc)});
    }

    void Else(SourceLocation loc, SourceLocation) override {
        if (!sm_.isWrittenInMainFile(loc)) {
            return;
        }
        directives_.push_back(
            {DirKind::Else, "", sm_.getSpellingLineNumber(loc)});
    }

    void Endif(SourceLocation loc, SourceLocation) override {
        if (!sm_.isWrittenInMainFile(loc)) {
            return;
        }
        directives_.push_back(
            {DirKind::Endif, "", sm_.getSpellingLineNumber(loc)});
    }

  private:
    SourceManager &sm_;
    std::vector<Directive> &directives_;
};

class LoweringAction final : public ASTFrontendAction {
  public:
    explicit LoweringAction(const std::string &outputPath)
        : outputPath_(outputPath) {}

    std::unique_ptr<ASTConsumer>
    CreateASTConsumer(CompilerInstance &, llvm::StringRef) override {
        return std::make_unique<ASTConsumer>();
    }

    bool BeginSourceFileAction(CompilerInstance &ci) override {
        auto &pp = ci.getPreprocessor();
        pp.addPPCallbacks(std::make_unique<LoweringPPCallbacks>(
            ci.getSourceManager(), directives_));
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
        emitHelper(out);

        std::vector<std::string> pcStack;
        std::vector<std::string> modeStack;
        int indentLevel = 0;
        int braceDepth = 0;
        std::optional<int> structDepth;
        int markerId = 0;
        std::optional<MacroGuard> macroGuard;

        for (size_t i = 0; i < lines.size(); ++i) {
            unsigned lineNo = static_cast<unsigned>(i + 1);
            auto it = directiveMap.find(lineNo);
            if (it != directiveMap.end()) {
                const auto &dir = it->second;
                if (dir.kind == DirKind::Ifdef || dir.kind == DirKind::Ifndef) {
                    std::string macro = dir.macro;
                    if (dir.kind == DirKind::Ifdef) {
                        pcStack.push_back(macro);
                    } else {
                        pcStack.push_back("!" + macro);
                    }

                    bool passMode = inStruct(structDepth, braceDepth) ||
                                    inTopLevel(structDepth, braceDepth);
                    modeStack.push_back(passMode ? "pass" : "if");

                    if (passMode) {
                        out.push_back("// PC: " + pcExpr(pcStack) + "\n");
                    } else {
                        out.push_back(indent(indentLevel) + "// PC: " +
                                      pcExpr(pcStack) + "\n");
                        out.push_back(indent(indentLevel) +
                                      (dir.kind == DirKind::Ifdef
                                           ? "if (__cfg(\"" + macro + "\")) {\n"
                                           : "if (!__cfg(\"" + macro + "\")) {\n"));
                        indentLevel += 1;
                        markerId += 1;
                        out.push_back(indent(indentLevel) + "PC_ANNOTATE(\"PC: " +
                                      pcExpr(pcStack) +
                                      "\") int __pc_marker_" + std::to_string(markerId) +
                                      ";\n");
                    }

                    if (inTopLevel(structDepth, braceDepth)) {
                        MacroGuard guard;
                        guard.macro = macro;
                        guard.negated = (dir.kind == DirKind::Ifndef);
                        macroGuard = guard;
                    }
                } else if (dir.kind == DirKind::Else) {
                    if (!pcStack.empty()) {
                        pcStack.back() = invert(pcStack.back());
                    }
                    if (!modeStack.empty() && modeStack.back() == "pass") {
                        out.push_back("// PC: " + pcExpr(pcStack) + "\n");
                    } else {
                        indentLevel -= 1;
                        out.push_back(indent(indentLevel) + "} else {\n");
                        indentLevel += 1;
                        markerId += 1;
                        out.push_back(indent(indentLevel) + "PC_ANNOTATE(\"PC: " +
                                      pcExpr(pcStack) +
                                      "\") int __pc_marker_" + std::to_string(markerId) +
                                      ";\n");
                    }

                    if (macroGuard) {
                        macroGuard->inElse = true;
                    }
                } else if (dir.kind == DirKind::Endif) {
                    if (!modeStack.empty() && modeStack.back() == "if") {
                        indentLevel -= 1;
                        out.push_back(indent(indentLevel) + "}\n");
                    }
                    if (macroGuard && inTopLevel(structDepth, braceDepth)) {
                        emitMacroGuard(out, *macroGuard);
                        macroGuard.reset();
                    }
                    if (!pcStack.empty()) {
                        pcStack.pop_back();
                    }
                    if (!modeStack.empty()) {
                        modeStack.pop_back();
                    }
                }
                continue;
            }

            if (macroGuard) {
                if (handleMacroDefine(lines[i], *macroGuard)) {
                    continue;
                }
            }

            std::string currentPc = pcExpr(pcStack);
            std::string outLine = lines[i];
            if (inStruct(structDepth, braceDepth)) {
                outLine = annotateStructField(outLine, currentPc);
            } else if (inTopLevel(structDepth, braceDepth) && currentPc != "TRUE" &&
                       isFunctionDef(outLine)) {
                markerId += 1;
                out.push_back("PC_ANNOTATE(\"PC: " + currentPc +
                              "\") int __pc_marker_" + std::to_string(markerId) +
                              ";\n");
            }

            out.push_back(outLine);

            if (isStructStart(outLine)) {
                int openBraces = countChar(outLine, '{');
                structDepth = braceDepth + openBraces;
            }

            braceDepth += countChar(outLine, '{');
            braceDepth -= countChar(outLine, '}');
            if (structDepth && braceDepth < *structDepth) {
                structDepth.reset();
            }
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

    static void emitHelper(std::vector<std::string> &out) {
        out.push_back("/* Symbolic lowering generated file */\n");
        out.push_back("#ifndef __SYMBOLIC_CFG_HELPER__\n");
        out.push_back("#define __SYMBOLIC_CFG_HELPER__\n");
        out.push_back("#include <stddef.h>\n");
        out.push_back("static volatile int __cfg_sink;\n");
        out.push_back("static __attribute__((noinline, optnone)) int __cfg(const char *name) {\n");
        out.push_back("    return __cfg_sink ^ (int)name[0];\n");
        out.push_back("}\n");
        out.push_back("#define PC_ANNOTATE(msg) __attribute__((annotate(msg)))\n");
        out.push_back("#endif\n\n");
    }

    static bool isStructStart(const std::string &line) {
        static const std::regex re(R"(\bstruct\s+\w+\s*\{)");
        return std::regex_search(line, re);
    }

    static bool isFunctionDef(const std::string &line) {
        static const std::regex re(R"(\w+\s*\([^;]*\)\s*\{)");
        return std::regex_search(line, re);
    }

    static bool inStruct(const std::optional<int> &structDepth, int braceDepth) {
        return structDepth && braceDepth >= *structDepth;
    }

    static bool inTopLevel(const std::optional<int> &structDepth, int braceDepth) {
        return braceDepth == 0 && !structDepth;
    }

    static std::string indent(int level) {
        return std::string(level * 4, ' ');
    }

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

    static int countChar(const std::string &line, char c) {
        int count = 0;
        for (char ch : line) {
            if (ch == c) {
                count++;
            }
        }
        return count;
    }

    static std::string annotateStructField(const std::string &line,
                                           const std::string &pc) {
        if (pc == "TRUE") {
            return line;
        }
        if (line.find(';') == std::string::npos) {
            return line;
        }
        if (line.find('(') != std::string::npos) {
            return line;
        }
        std::string trimmed = ltrim(line);
        if (startsWith(trimmed, "}") || startsWith(trimmed, "/")) {
            return line;
        }
        auto pos = line.rfind(';');
        if (pos == std::string::npos) {
            return line;
        }
        std::string prefix = rtrim(line.substr(0, pos));
        std::string suffix = line.substr(pos);
        return prefix + " PC_ANNOTATE(\"PC: " + pc + "\")" + suffix;
    }

    static std::string ltrim(const std::string &s) {
        size_t i = 0;
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) {
            ++i;
        }
        return s.substr(i);
    }

    static std::string rtrim(const std::string &s) {
        size_t end = s.size();
        while (end > 0 && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
            --end;
        }
        return s.substr(0, end);
    }

    static bool startsWith(const std::string &s, const std::string &prefix) {
        return s.rfind(prefix, 0) == 0;
    }

    static bool handleMacroDefine(const std::string &line, MacroGuard &guard) {
        static const std::regex re(
            R"(^\s*#\s*define\s+(\w+)\s*\(([^)]*)\)\s*(.*)$)");
        std::smatch match;
        if (!std::regex_match(line, match, re)) {
            return false;
        }
        guard.name = match[1].str();
        guard.params = match[2].str();
        std::string body = match[3].str();
        if (guard.inElse) {
            guard.elseBody = body;
        } else {
            guard.ifBody = body;
        }
        return true;
    }

    static void emitMacroGuard(std::vector<std::string> &out,
                               const MacroGuard &guard) {
        if (guard.name.empty()) {
            return;
        }
        std::string cond = "__cfg(\"" + guard.macro + "\")";
        if (guard.negated) {
            cond = "!" + cond;
        }
        std::string ifBody = ensureSemicolon(guard.ifBody);
        std::string elseBody = ensureSemicolon(guard.elseBody);
        out.push_back("#define " + guard.name + "(" + guard.params +
                      ") do { if (" + cond + ") { " + ifBody +
                      " } else { " + elseBody + " } } while(0)\n");
    }

    static std::string ensureSemicolon(const std::string &body) {
        std::string trimmed = rtrim(body);
        if (trimmed.empty()) {
            return "";
        }
        if (!trimmed.empty() && trimmed.back() == ';') {
            return trimmed;
        }
        return trimmed + ";";
    }
};

} // namespace

static llvm::cl::OptionCategory ToolCategory("symbolic-lowering-clang");
static llvm::cl::opt<std::string> OutputPath(
    "output", llvm::cl::desc("Output C file"), llvm::cl::value_desc("path"),
    llvm::cl::Required, llvm::cl::cat(ToolCategory));

int main(int argc, const char **argv) {
    auto expectedParser = CommonOptionsParser::create(argc, argv, ToolCategory);
    if (!expectedParser) {
        llvm::errs() << expectedParser.takeError();
        return 1;
    }

    CommonOptionsParser &optionsParser = expectedParser.get();
    std::vector<std::string> sources = optionsParser.getSourcePathList();
    if (sources.size() != 1) {
        llvm::errs() << "Expected a single input source file\n";
        return 1;
    }

    std::vector<std::string> args = {"-x", "c", "-std=c11"};
    FixedCompilationDatabase compDb(".", args);
    ClangTool tool(compDb, sources);

        class LoweringActionFactory : public FrontendActionFactory {
            public:
                explicit LoweringActionFactory(std::string outputPath)
                        : outputPath_(std::move(outputPath)) {}

                std::unique_ptr<FrontendAction> create() override {
                        return std::make_unique<LoweringAction>(outputPath_);
                }

            private:
                std::string outputPath_;
        };

        auto factory = std::make_unique<LoweringActionFactory>(OutputPath);
        return tool.run(factory.get());
}
