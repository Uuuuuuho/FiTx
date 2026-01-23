#include <llvm/Support/raw_ostream.h>

#include <algorithm>
#include <cctype>
#include <regex>
#include <sstream>

#include "PcLowerPluginAction.h"

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
        std::make_unique<PPCapturer>(ci.getSourceManager(), directives_));
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
    static const std::regex structStartRe(R"(\bstruct\s+\w+\s*\{)");
    static const std::regex funcDefRe(R"(\w+\s*\([^;]*\)\s*\{)");

    std::vector<std::string> out;
    std::vector<std::string> stack;
    std::vector<std::string> modeStack;
    int indentLevel = 0;
    int braceDepth = 0;
    std::optional<int> structDepth;
    std::optional<MacroGuard> macroGuard;

    emitHelper(out);

    for (size_t i = 0; i < lines.size(); ++i) {
        unsigned lineNo = static_cast<unsigned>(i + 1);
        auto it = directiveMap.find(lineNo);
        if (it != directiveMap.end()) {
            const auto &dir = it->second;
            if (dir.kind == DirEnum::Ifdef || dir.kind == DirEnum::Ifndef) {
                std::string macro = dir.macro;
                if (dir.kind == DirEnum::Ifdef) {
                    stack.push_back(macro);
                } else {
                    stack.push_back("!" + macro);
                }

                bool passMode = inStruct(structDepth, braceDepth) ||
                                inTopLevel(structDepth, braceDepth);
                modeStack.push_back(passMode ? "pass" : "if");

                if (inTopLevel(structDepth, braceDepth)) {
                    MacroGuard guard;
                    guard.macro = macro;
                    guard.negated = (dir.kind == DirEnum::Ifndef);
                    macroGuard = guard;
                }

                if (passMode) {
                    if (macroGuard && inTopLevel(structDepth, braceDepth)) {
                        macroGuard->ifCommentIndex = out.size();
                    }
                    out.push_back("// PC: " + pcExpr(stack) + "\n");
                } else {
                    out.push_back(indent(indentLevel) + "// PC: " +
                                  pcExpr(stack) + "\n");
                    out.push_back(indent(indentLevel) +
                                  (dir.kind == DirEnum::Ifdef
                                       ? "if (__cfg(\"" + macro + "\")) {\n"
                                       : "if (!__cfg(\"" + macro + "\")) {\n"));
                    indentLevel += 1;
                }

                
            } else if (dir.kind == DirEnum::Else) {
                if (!stack.empty()) {
                    stack.back() = invert(stack.back());
                }
                if (!modeStack.empty() && modeStack.back() == "pass") {
                    if (!(macroGuard && macroGuard->hasDefine &&
                          inTopLevel(structDepth, braceDepth))) {
                        if (macroGuard && inTopLevel(structDepth, braceDepth)) {
                            macroGuard->elseCommentIndex = out.size();
                        }
                        out.push_back("// PC: " + pcExpr(stack) + "\n");
                    }
                } else {
                    indentLevel -= 1;
                    out.push_back(indent(indentLevel) + "} else {\n");
                    indentLevel += 1;
                }
                if (macroGuard) {
                    macroGuard->inElse = true;
                }
            } else if (dir.kind == DirEnum::Endif) {
                if (!modeStack.empty() && modeStack.back() == "if") {
                    indentLevel -= 1;
                    out.push_back(indent(indentLevel) + "}\n");
                }
                if (macroGuard && inTopLevel(structDepth, braceDepth)) {
                    emitMacroGuard(out, *macroGuard);
                    macroGuard.reset();
                }
                if (!stack.empty()) {
                    stack.pop_back();
                }
                if (!modeStack.empty()) {
                    modeStack.pop_back();
                }
            }
            continue;
        }

        if (macroGuard) {
            if (handleMacroDefine(lines[i], *macroGuard, out)) {
                continue;
            }
        }

        std::string outLine = lines[i];

        out.push_back(outLine);

        if (std::regex_search(outLine, structStartRe)) {
            int openBraces = countChar(outLine, '{');
            structDepth = braceDepth + openBraces;
        }

        braceDepth += countChar(outLine, '{');
        braceDepth -= countChar(outLine, '}');
        if (structDepth && braceDepth < *structDepth) {
            structDepth.reset();
        }
    }

    return out;
}

void PcLowerPluginAction::emitHelper(std::vector<std::string> &out) {
    out.push_back("/* Symbolic lowering generated file */\n");
    out.push_back("#ifndef __SYMBOLIC_CFG_HELPER__\n");
    out.push_back("#define __SYMBOLIC_CFG_HELPER__\n");
    out.push_back("#include <stddef.h>\n");
    out.push_back("static volatile int __cfg_sink;\n");
    out.push_back(
        "static __attribute__((noinline, optnone)) int __cfg(const char *name) {\n");
    out.push_back("    return __cfg_sink ^ (int)name[0];\n");
    out.push_back("}\n");
    out.push_back("#endif\n\n");
}

std::string PcLowerPluginAction::indent(int level) {
    return std::string(level * 4, ' ');
}

bool PcLowerPluginAction::inStruct(const std::optional<int> &structDepth,
                                int braceDepth) {
    return structDepth && braceDepth >= *structDepth;
}

bool PcLowerPluginAction::inTopLevel(const std::optional<int> &structDepth,
                                  int braceDepth) {
    return braceDepth == 0 && !structDepth;
}

int PcLowerPluginAction::countChar(const std::string &line, char c) {
    int count = 0;
    for (char ch : line) {
        if (ch == c) {
            count++;
        }
    }
    return count;
}

std::string PcLowerPluginAction::rtrim(const std::string &s) {
    size_t end = s.size();
    while (end > 0 && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }
    return s.substr(0, end);
}


bool PcLowerPluginAction::handleMacroDefine(const std::string &line,
                                         MacroGuard &guard,
                                         std::vector<std::string> &out) {
    std::smatch match;
    static const std::regex funcRe(
        R"(^\s*#\s*define\s+(\w+)\s*\(([^)]*)\)\s*(.*)$)");
    static const std::regex objRe(
        R"(^\s*#\s*define\s+(\w+)\s*(.*)$)");
    std::string name;
    std::string params;
    std::string body;
    std::string trimmedLine = rtrim(line);
    if (std::regex_match(trimmedLine, match, funcRe)) {
        name = match[1].str();
        params = match[2].str();
        body = match[3].str();
    } else if (std::regex_match(trimmedLine, match, objRe)) {
        name = match[1].str();
        params = "";
        body = match[2].str();
    } else {
        return false;
    }
    if (!guard.name.empty() && guard.name != name) {
        return false;
    }
    guard.name = name;
    if (guard.params.empty()) {
        guard.params = params;
    }
    if (!guard.hasDefine) {
        guard.hasDefine = true;
        std::vector<size_t> indices;
        if (guard.ifCommentIndex) {
            indices.push_back(*guard.ifCommentIndex);
        }
        if (guard.elseCommentIndex) {
            indices.push_back(*guard.elseCommentIndex);
        }
        std::sort(indices.begin(), indices.end(), std::greater<size_t>());
        for (size_t index : indices) {
            if (index < out.size()) {
                out.erase(out.begin() + static_cast<long>(index));
            }
        }
        guard.ifCommentIndex.reset();
        guard.elseCommentIndex.reset();
    }
    if (guard.inElse) {
        guard.elseBody = body;
    } else {
        guard.ifBody = body;
    }
    return true;
}

std::string PcLowerPluginAction::ensureSemicolon(const std::string &body) {
    std::string trimmed = rtrim(body);
    if (trimmed.empty()) {
        return "";
    }
    if (!trimmed.empty() && trimmed.back() == ';') {
        return trimmed;
    }
    return trimmed + ";";
}

void PcLowerPluginAction::emitMacroGuard(std::vector<std::string> &out,
                                      const MacroGuard &guard) {
    if (guard.name.empty()) {
        return;
    }
    std::string cond = "__cfg(\"" + guard.macro + "\")";
    if (guard.negated) {
        cond = "!" + cond;
    }
    if (guard.hasDefine) {
        out.push_back("// PC: " + guard.macro + " | !" + guard.macro + "\n");
    }
    std::string ifBody = ensureSemicolon(guard.ifBody);
    std::string elseBody = ensureSemicolon(guard.elseBody);
    std::string macroSig = guard.name;
    if (!guard.params.empty()) {
        macroSig += "(" + guard.params + ")";
    }
    out.push_back("#define " + macroSig + " do { if (" + cond + ") { " +
                  ifBody + " } else { " + elseBody + " } } while(0)\n");
}

} // namespace pclower

static clang::FrontendPluginRegistry::Add<pclower::PcLowerPluginAction>
    X("pclower", "Symbolic lowering and PC propagation");