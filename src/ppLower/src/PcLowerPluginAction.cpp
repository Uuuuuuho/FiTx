#include <llvm/Support/raw_ostream.h>

#include "PcLowerPluginAction.h"
#include "LoweredRenderer.h"

#include <sstream>

namespace pclower {

bool PcLowerPluginAction::ParseArgs(const clang::CompilerInstance &,
                                 const std::vector<std::string> &args) {
    for (const auto &arg : args) {
        if (arg.rfind("pc-out=", 0) == 0) {
            pcOut_ = arg.substr(std::string("pc-out=").size());
        } else if (arg == "pc-off") {
            emitPc_ = false;
        } else if (arg == "pc-on") {
            emitPc_ = true;
        } else if (arg.rfind("lowered-out=", 0) == 0) {
            loweredOut_ = arg.substr(std::string("lowered-out=").size());
        } else if (arg == "lowered-off") {
            emitLowered_ = false;
        } else if (arg == "lowered-on") {
            emitLowered_ = true;
        }
    }
    return true;
}

std::unique_ptr<clang::ASTConsumer>
PcLowerPluginAction::CreateASTConsumer(clang::CompilerInstance &ci,
                                    llvm::StringRef) {
    ci.getPreprocessor().addPPCallbacks(
        std::make_unique<PPCapturer>(ci.getSourceManager(), ci.getLangOpts(), directives_));
    return std::make_unique<clang::ASTConsumer>();
}

void PcLowerPluginAction::EndSourceFileAction() {
    clang::SourceManager &sm = getCompilerInstance().getSourceManager();
    clang::FileID mainFile = sm.getMainFileID();
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

    if (emitPc_ && !pcOut_.empty()) { // debug for PC rendering
        auto pc = renderPC(lines, directiveMap);
        writeFile(pcOut_, pc);
    }
    if (emitLowered_ && !loweredOut_.empty()) { // debug for lowered rendering
        auto lowered = renderLowered(lines, directiveMap);
        writeFile(loweredOut_, lowered);
    }
}

void PcLowerPluginAction::writeFile(const std::string &path,
                                 const std::vector<std::string> &lines) {
    std::error_code ec;
    llvm::raw_fd_ostream out(path, ec);
    if (ec) {
        llvm::errs() << "Failed to write " << path << ": " << ec.message()
                     << "\n";
        return;
    }
    for (const auto &line : lines) {
        out << line;
    }
}

std::string PcLowerPluginAction::pcExpr(const std::vector<std::string> &stack) {
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

std::string PcLowerPluginAction::invert(const std::string &expr) {
    if (!expr.empty() && expr[0] == '!') {
        return expr.substr(1);
    }
    return "!" + expr;
}

std::vector<std::string> PcLowerPluginAction::renderPC(
    const std::vector<std::string> &lines,
    const std::map<unsigned, Directive> &directiveMap) {
    std::vector<std::string> out;
    std::vector<std::string> stack;

    for (size_t i = 0; i < lines.size(); ++i) {
        unsigned lineNo = static_cast<unsigned>(i + 1);
        auto it = directiveMap.find(lineNo);
        if (it != directiveMap.end()) {
            const auto &dir = it->second;
            if (dir.kind == DirEnum::Ifdef) {
                stack.push_back(dir.macro);
                out.push_back("// PC-BEGIN: " + pcExpr(stack) + "\n");
            } else if (dir.kind == DirEnum::Ifndef) {
                stack.push_back("!" + dir.macro);
                out.push_back("// PC-BEGIN: " + pcExpr(stack) + "\n");
            } else if (dir.kind == DirEnum::Else) {
                if (!stack.empty()) {
                    stack.back() = invert(stack.back());
                }
                out.push_back("// PC-ELSE: " + pcExpr(stack) + "\n");
            } else if (dir.kind == DirEnum::Endif) {
                if (!stack.empty()) {
                    stack.pop_back();
                }
                out.push_back("// PC-END: " + pcExpr(stack) + "\n");
            }
            continue;
        }

        std::string currentPc = pcExpr(stack);
        if (currentPc != "TRUE") {
            out.push_back("// PC: " + currentPc + "\n");
        }
        out.push_back(lines[i]);
    }

    return out;
}

std::vector<std::string> PcLowerPluginAction::renderLowered(
    const std::vector<std::string> &lines,
    const std::map<unsigned, Directive> &directiveMap) {
    LoweredRenderer renderer(lines, directiveMap);
    return renderer.render();
}

} // namespace pclower

static clang::FrontendPluginRegistry::Add<pclower::PcLowerPluginAction>
    X("pclower", "Symbolic lowering and PC propagation");
