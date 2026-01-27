#include "LoweredRenderer.h"

#include <algorithm>
#include <cctype>
#include <regex>
#include <sstream>

namespace pclower {

LoweredRenderer::LoweredRenderer(const std::vector<std::string> &lines,
                                 const std::map<unsigned, Directive> &directiveMap)
    : lines_(lines), directiveMap_(directiveMap) {}

std::vector<std::string> LoweredRenderer::render() {
    static const std::regex funcDefRe(R"(\w+\s*\([^;]*\)\s*\{)");

    emitHelper(out_);

    for (size_t i = 0; i < lines_.size(); ++i) {
        unsigned lineNo = static_cast<unsigned>(i + 1);
        auto it = directiveMap_.find(lineNo);
        if (it != directiveMap_.end()) {
            handleDirective(it->second.kind, it->second.macro);
            continue;
        }

        DirEnum rawKind;
        std::string rawMacro;
        if (parseRawDirective(lines_[i], rawKind, rawMacro)) {
            if (lines_[i].find("elif") != std::string::npos) {
                handleDirective(DirEnum::Else, "");
            }
            handleDirective(rawKind, rawMacro);
            continue;
        }

        if (macroGuard_) {
            if (handleMacroDefine(lines_[i], *macroGuard_, out_)) {
                continue;
            }
        }

        if (collectingFunc_) {
            funcLines_.push_back(lines_[i]);
            if (lines_[i].find("/*") != std::string::npos ||
                lines_[i].find("*/") != std::string::npos) {
                abortCollect_ = true;
            }
            if (lines_[i].find(';') != std::string::npos) {
                abortCollect_ = true;
            }
            if (lines_[i].find('{') != std::string::npos || abortCollect_) {
                if (tryHandleCollectedFuncSignature()) {
                    continue;
                }
                flushCollectedFuncLines();
                continue;
            }
            continue;
        }

        std::string outLine = lines_[i];
        if (macroGuard_ && inTopLevel(structDepth_, braceDepth_) &&
            std::regex_search(outLine, funcDefRe)) {
            std::string prefix;
            std::string name;
            std::string params;
            std::string suffix;
            if (extractFunctionSignature(outLine, prefix, name, params, suffix)) {
                std::string newName = name + "__pclower_" + macroGuard_->macro +
                                      (macroGuard_->inElse ? "_else" : "_if");
                outLine = prefix + newName + "(" + params + ")" + suffix;
                auto &funcs = macroGuard_->functions;
                auto existing = std::find_if(
                    funcs.begin(), funcs.end(),
                    [&](const MacroGuard::GuardedFunction &f) { return f.name == name; });
                if (existing == funcs.end()) {
                    MacroGuard::GuardedFunction func;
                    func.name = name;
                    func.prefix = prefix;
                    func.params = params;
                    if (macroGuard_->inElse) {
                        func.elseName = newName;
                        func.hasElse = true;
                    } else {
                        func.ifName = newName;
                        func.hasIf = true;
                    }
                    std::string proto = prefix + name + "(" + params + ");\n";
                    out_.push_back(proto);
                    func.protoEmitted = true;
                    funcs.push_back(func);
                } else {
                    if (!existing->protoEmitted) {
                        std::string proto =
                            existing->prefix + name + "(" + existing->params + ");\n";
                        out_.push_back(proto);
                        existing->protoEmitted = true;
                    }
                    if (macroGuard_->inElse) {
                        existing->elseName = newName;
                        existing->hasElse = true;
                    } else {
                        existing->ifName = newName;
                        existing->hasIf = true;
                    }
                }
            }
        }

        if (macroGuard_ && inTopLevel(structDepth_, braceDepth_) &&
            isLikelyFuncStart(outLine) && outLine.find("typedef") == std::string::npos &&
            outLine.find('=') == std::string::npos && outLine.find(';') == std::string::npos &&
            outLine.find('{') == std::string::npos &&
            outLine.find("__must_hold") == std::string::npos &&
            outLine.find("__acquires") == std::string::npos &&
            outLine.find("__releases") == std::string::npos) {
            collectingFunc_ = true;
            funcLines_.push_back(outLine);
            continue;
        }

        out_.push_back(outLine);
        updateStructDepthForLine(outLine);
    }

    return out_;
}

void LoweredRenderer::handleDirective(DirEnum kind, const std::string &macro) {
    if (kind == DirEnum::Ifdef || kind == DirEnum::Ifndef) {
        if (kind == DirEnum::Ifdef) {
            stack_.push_back(macro);
        } else {
            stack_.push_back("!" + macro);
        }

        bool passMode = inStruct(structDepth_, braceDepth_) ||
                        inTopLevel(structDepth_, braceDepth_);
        modeStack_.push_back(passMode ? "pass" : "if");

        if (inTopLevel(structDepth_, braceDepth_)) {
            MacroGuard guard;
            guard.macro = macro;
            guard.negated = (kind == DirEnum::Ifndef);
            macroGuard_ = guard;
        }

        if (passMode) {
            if (macroGuard_ && inTopLevel(structDepth_, braceDepth_)) {
                macroGuard_->ifCommentIndex = out_.size();
            }
            out_.push_back("// PC: " + pcExpr(stack_) + "\n");
        } else {
            out_.push_back(indent(indentLevel_) + "// PC: " + pcExpr(stack_) + "\n");
            out_.push_back(indent(indentLevel_) +
                           (kind == DirEnum::Ifdef
                                ? "if (__cfg(\"" + macro + "\")) {\n"
                                : "if (!__cfg(\"" + macro + "\")) {\n"));
            indentLevel_ += 1;
        }

    } else if (kind == DirEnum::Else) {
        if (!stack_.empty()) {
            stack_.back() = invert(stack_.back());
        }
        if (!modeStack_.empty() && modeStack_.back() == "pass") {
            if (!(macroGuard_ && macroGuard_->hasDefine &&
                  inTopLevel(structDepth_, braceDepth_))) {
                if (macroGuard_ && inTopLevel(structDepth_, braceDepth_)) {
                    macroGuard_->elseCommentIndex = out_.size();
                }
                out_.push_back("// PC: " + pcExpr(stack_) + "\n");
            }
        } else {
            indentLevel_ -= 1;
            out_.push_back(indent(indentLevel_) + "} else {\n");
            indentLevel_ += 1;
        }
        if (macroGuard_) {
            macroGuard_->inElse = true;
        }
    } else if (kind == DirEnum::Endif) {
        if (!modeStack_.empty() && modeStack_.back() == "if") {
            indentLevel_ -= 1;
            out_.push_back(indent(indentLevel_) + "}\n");
        }
        if (macroGuard_ && inTopLevel(structDepth_, braceDepth_)) {
            emitMacroGuard(out_, *macroGuard_);
            macroGuard_.reset();
        }
        if (!stack_.empty()) {
            stack_.pop_back();
        }
        if (!modeStack_.empty()) {
            modeStack_.pop_back();
        }
    }
}

bool LoweredRenderer::parseRawDirective(const std::string &line,
                                        DirEnum &kind,
                                        std::string &macro) {
    size_t pos = 0;
    while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos]))) {
        ++pos;
    }
    if (pos >= line.size() || line[pos] != '#') {
        return false;
    }
    ++pos;
    while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos]))) {
        ++pos;
    }
    if (line.compare(pos, 4, "else") == 0) {
        kind = DirEnum::Else;
        macro.clear();
        return true;
    }
    if (line.compare(pos, 5, "endif") == 0) {
        kind = DirEnum::Endif;
        macro.clear();
        return true;
    }
    bool isIf = line.compare(pos, 2, "if") == 0;
    bool isElif = line.compare(pos, 4, "elif") == 0;
    if (!isIf && !isElif) {
        return false;
    }
    static const std::regex macroRe(R"(CONFIG_[A-Za-z0-9_]+)");
    std::smatch match;
    if (!std::regex_search(line, match, macroRe)) {
        return false;
    }
    macro = match[0].str();
    bool negated = false;
    size_t mpos = line.find(macro);
    if (mpos != std::string::npos) {
        size_t i = mpos;
        while (i > 0 && std::isspace(static_cast<unsigned char>(line[i - 1]))) {
            --i;
        }
        if (i > 0 && line[i - 1] == '!') {
            negated = true;
        }
    }
    if (line.find("!defined") != std::string::npos ||
        line.find("! IS_ENABLED") != std::string::npos ||
        line.find("!IS_ENABLED") != std::string::npos) {
        negated = true;
    }
    kind = negated ? DirEnum::Ifndef : DirEnum::Ifdef;
    if (isElif) {
        return true;
    }
    return true;
}

bool LoweredRenderer::tryHandleCollectedFuncSignature() {
    std::string merged;
    for (const auto &line : funcLines_) {
        std::string trimmed = rtrim(line);
        if (!trimmed.empty()) {
            if (!merged.empty()) {
                merged += " ";
            }
            merged += trimmed;
        }
    }
    if (!abortCollect_ && macroGuard_ && inTopLevel(structDepth_, braceDepth_)) {
        std::string prefix;
        std::string name;
        std::string params;
        std::string suffix;
        if (extractFunctionSignature(merged, prefix, name, params, suffix)) {
            std::string newName =
                name + "__pclower_" + macroGuard_->macro +
                (macroGuard_->inElse ? "_else" : "_if");
            std::string newLine = prefix + newName + "(" + params + ")" + suffix + "\n";
            auto &funcs = macroGuard_->functions;
            auto existing = std::find_if(
                funcs.begin(), funcs.end(),
                [&](const MacroGuard::GuardedFunction &f) { return f.name == name; });
            if (existing == funcs.end()) {
                MacroGuard::GuardedFunction func;
                func.name = name;
                func.prefix = prefix;
                func.params = params;
                if (macroGuard_->inElse) {
                    func.elseName = newName;
                    func.hasElse = true;
                } else {
                    func.ifName = newName;
                    func.hasIf = true;
                }
                std::string proto = prefix + name + "(" + params + ");\n";
                out_.push_back(proto);
                func.protoEmitted = true;
                funcs.push_back(func);
            } else {
                if (!existing->protoEmitted) {
                    std::string proto =
                        existing->prefix + name + "(" + existing->params + ");\n";
                    out_.push_back(proto);
                    existing->protoEmitted = true;
                }
                if (macroGuard_->inElse) {
                    existing->elseName = newName;
                    existing->hasElse = true;
                } else {
                    existing->ifName = newName;
                    existing->hasIf = true;
                }
            }
            out_.push_back(newLine);
            updateStructDepthForLine(newLine);
            funcLines_.clear();
            collectingFunc_ = false;
            abortCollect_ = false;
            return true;
        }
    }
    return false;
}

void LoweredRenderer::flushCollectedFuncLines() {
    for (const auto &line : funcLines_) {
        out_.push_back(line);
        updateStructDepthForLine(line);
    }
    funcLines_.clear();
    collectingFunc_ = false;
    abortCollect_ = false;
}

void LoweredRenderer::updateStructDepthForLine(const std::string &line) {
    static const std::regex structStartRe(R"(\bstruct\s+\w+\s*\{)");
    if (!isPreprocessorLine(line) && std::regex_search(line, structStartRe)) {
        int openBraces = countChar(line, '{');
        structDepth_ = braceDepth_ + openBraces;
    }
    if (!isPreprocessorLine(line)) {
        braceDepth_ += countChar(line, '{');
        braceDepth_ -= countChar(line, '}');
    }
    if (structDepth_ && braceDepth_ < *structDepth_) {
        structDepth_.reset();
    }
}

std::string LoweredRenderer::pcExpr(const std::vector<std::string> &stack) {
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

std::string LoweredRenderer::invert(const std::string &expr) {
    if (!expr.empty() && expr[0] == '!') {
        return expr.substr(1);
    }
    return "!" + expr;
}

void LoweredRenderer::emitHelper(std::vector<std::string> &out) {
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

std::string LoweredRenderer::indent(int level) {
    return std::string(level * 4, ' ');
}

bool LoweredRenderer::inStruct(const std::optional<int> &structDepth, int braceDepth) {
    return structDepth && braceDepth >= *structDepth;
}

bool LoweredRenderer::inTopLevel(const std::optional<int> &structDepth, int braceDepth) {
    return braceDepth == 0 && !structDepth;
}

int LoweredRenderer::countChar(const std::string &line, char c) {
    int count = 0;
    for (char ch : line) {
        if (ch == c) {
            count++;
        }
    }
    return count;
}

std::string LoweredRenderer::rtrim(const std::string &s) {
    size_t end = s.size();
    while (end > 0 && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }
    return s.substr(0, end);
}

std::string LoweredRenderer::stripTrailingSemicolon(const std::string &body) {
    std::string trimmed = rtrim(body);
    if (!trimmed.empty() && trimmed.back() == ';') {
        trimmed.pop_back();
    }
    return rtrim(trimmed);
}

std::string LoweredRenderer::buildCallArgs(const std::string &params) {
    if (params.empty()) {
        return "";
    }
    std::vector<std::string> tokens;
    std::stringstream ss(params);
    std::string token;
    while (std::getline(ss, token, ',')) {
        size_t start = 0;
        while (start < token.size() && std::isspace(static_cast<unsigned char>(token[start]))) {
            ++start;
        }
        size_t end = token.size();
        while (end > start && std::isspace(static_cast<unsigned char>(token[end - 1]))) {
            --end;
        }
        std::string t = token.substr(start, end - start);
        if (t == "...") {
            tokens.push_back("__VA_ARGS__");
        } else if (!t.empty()) {
            tokens.push_back(t);
        }
    }
    std::string out;
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (i > 0) {
            out += ", ";
        }
        out += tokens[i];
    }
    return out;
}

bool LoweredRenderer::isSimpleValueMacro(const std::string &body) {
    std::string trimmed = rtrim(body);
    if (trimmed.empty()) {
        return false;
    }
    if (trimmed.find(';') != std::string::npos || trimmed.find('{') != std::string::npos ||
        trimmed.find('}') != std::string::npos) {
        return false;
    }
    static const std::regex valueRe(
        R"(^[\w\s\(\)\*\+\-\/%%&\|\^!<>=\?:\.]+$)");
    return std::regex_match(trimmed, valueRe);
}

bool LoweredRenderer::extractFunctionSignature(const std::string &line,
                                               std::string &prefix,
                                               std::string &name,
                                               std::string &params,
                                               std::string &suffix) {
    size_t lparen = line.find('(');
    if (lparen == std::string::npos) {
        return false;
    }
    size_t rparen = line.find(')', lparen + 1);
    if (rparen == std::string::npos) {
        return false;
    }
    size_t nameEnd = lparen;
    size_t namePos = nameEnd;
    while (namePos > 0 && std::isspace(static_cast<unsigned char>(line[namePos - 1]))) {
        --namePos;
    }
    size_t nameStart = namePos;
    while (nameStart > 0 &&
           (std::isalnum(static_cast<unsigned char>(line[nameStart - 1])) ||
            line[nameStart - 1] == '_')) {
        --nameStart;
    }
    if (nameStart == namePos) {
        return false;
    }
    prefix = line.substr(0, nameStart);
    name = line.substr(nameStart, namePos - nameStart);
    params = line.substr(lparen + 1, rparen - lparen - 1);
    suffix = line.substr(rparen + 1);
    return true;
}

std::string LoweredRenderer::buildWrapperParamList(const std::string &params,
                                                   std::vector<std::string> &argNames) {
    argNames.clear();
    std::string trimmed = rtrim(params);
    if (trimmed.empty()) {
        return "";
    }
    std::string trimmedLeft = trimmed;
    size_t start = 0;
    while (start < trimmedLeft.size() &&
           std::isspace(static_cast<unsigned char>(trimmedLeft[start]))) {
        ++start;
    }
    trimmedLeft = trimmedLeft.substr(start);
    if (trimmedLeft == "void") {
        return "void";
    }
    std::vector<std::string> tokens;
    std::stringstream ss(trimmedLeft);
    std::string token;
    while (std::getline(ss, token, ',')) {
        size_t tstart = 0;
        while (tstart < token.size() &&
               std::isspace(static_cast<unsigned char>(token[tstart]))) {
            ++tstart;
        }
        size_t tend = token.size();
        while (tend > tstart &&
               std::isspace(static_cast<unsigned char>(token[tend - 1]))) {
            --tend;
        }
        std::string t = token.substr(tstart, tend - tstart);
        if (t.empty()) {
            continue;
        }
        if (t == "...") {
            return params;
        }
        size_t end = t.size();
        while (end > 0 &&
               !std::isalnum(static_cast<unsigned char>(t[end - 1])) && t[end - 1] != '_') {
            --end;
        }
        size_t nameEnd = end;
        size_t nameStart = nameEnd;
        while (nameStart > 0 &&
               (std::isalnum(static_cast<unsigned char>(t[nameStart - 1])) ||
                t[nameStart - 1] == '_')) {
            --nameStart;
        }
        std::string paramName;
        if (nameStart < nameEnd) {
            paramName = t.substr(nameStart, nameEnd - nameStart);
        }
        if (paramName.empty() || paramName == "void") {
            paramName = "pclower_arg" + std::to_string(argNames.size());
            t += " " + paramName;
        }
        argNames.push_back(paramName);
        tokens.push_back(t);
    }
    std::string out;
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (i > 0) {
            out += ", ";
        }
        out += tokens[i];
    }
    return out;
}

bool LoweredRenderer::isVoidReturnPrefix(const std::string &prefix) {
    std::string trimmed = rtrim(prefix);
    while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.back()))) {
        trimmed.pop_back();
    }
    if (trimmed.empty()) {
        return false;
    }
    if (trimmed.back() == '*') {
        return false;
    }
    size_t end = trimmed.size();
    size_t start = end;
    while (start > 0 &&
           (std::isalnum(static_cast<unsigned char>(trimmed[start - 1])) ||
            trimmed[start - 1] == '_')) {
        --start;
    }
    if (start == end) {
        return false;
    }
    std::string lastToken = trimmed.substr(start, end - start);
    return lastToken == "void";
}

bool LoweredRenderer::handleMacroDefine(const std::string &line,
                                        MacroGuard &guard,
                                        std::vector<std::string> &out) {
    std::smatch match;
    static const std::regex funcRe(
        R"(^\s*#\s*define\s+(\w+)\s*\(([^)]*)\)\s*(.*)$)");
    static const std::regex objRe(R"(^\s*#\s*define\s+(\w+)\s*(.*)$)");
    std::string name;
    std::string params;
    std::string body;
    bool functionLike = false;
    std::string trimmedLine = rtrim(line);
    if (std::regex_match(trimmedLine, match, funcRe)) {
        name = match[1].str();
        params = match[2].str();
        body = match[3].str();
        functionLike = true;
    } else if (std::regex_match(trimmedLine, match, objRe)) {
        name = match[1].str();
        params = "";
        body = match[2].str();
    } else {
        return false;
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
    if (guard.name.empty() || guard.name == name) {
        guard.name = name;
        guard.functionLike = functionLike;
        if (guard.params.empty()) {
            guard.params = params;
        }
        if (guard.inElse) {
            guard.elseBody = body;
            guard.elseDefined = true;
        } else {
            guard.ifBody = body;
            guard.ifDefined = true;
        }
        return true;
    }
    auto it = std::find_if(
        guard.extraDefines.begin(), guard.extraDefines.end(),
        [&](const MacroGuard::MacroDefine &def) { return def.name == name; });
    if (it == guard.extraDefines.end()) {
        MacroGuard::MacroDefine def;
        def.name = name;
        def.params = params;
        def.functionLike = functionLike;
        guard.extraDefines.push_back(def);
        it = guard.extraDefines.end() - 1;
    }
    if (guard.inElse) {
        it->elseBody = body;
        it->elseDefined = true;
    } else {
        it->ifBody = body;
        it->ifDefined = true;
    }
    return true;
}

std::string LoweredRenderer::ensureSemicolon(const std::string &body) {
    std::string trimmed = rtrim(body);
    if (trimmed.empty()) {
        return "";
    }
    if (!trimmed.empty() && trimmed.back() == ';') {
        return trimmed;
    }
    return trimmed + ";";
}

void LoweredRenderer::emitMacroGuard(std::vector<std::string> &out,
                                     const MacroGuard &guard) {
    if (guard.name.empty() && guard.extraDefines.empty() && guard.functions.empty()) {
        return;
    }
    std::string cond = "__cfg(\"" + guard.macro + "\")";
    if (guard.negated) {
        cond = "!" + cond;
    }
    auto emitDefine = [&](const MacroGuard::MacroDefine &def, bool &pcCommentEmitted) {
        if (def.name.empty() || (!def.ifDefined && !def.elseDefined)) {
            return;
        }
        const MacroGuard::GuardedFunction *macroFunc = nullptr;
        for (const auto &func : guard.functions) {
            if (func.name == def.name) {
                macroFunc = &func;
                break;
            }
        }
        if (macroFunc && !def.functionLike) {
            return;
        }
        if (guard.hasDefine && !pcCommentEmitted) {
            out.push_back("// PC: " + guard.macro + " | !" + guard.macro + "\n");
            pcCommentEmitted = true;
        }
        std::string macroSig = def.name;
        if (def.functionLike) {
            macroSig += "(" + def.params + ")";
        } else if (!def.params.empty()) {
            macroSig += "(" + def.params + ")";
        }
        if (def.functionLike) {
            std::string callArgs = buildCallArgs(def.params);
            std::string callTarget = def.name;
            if (macroFunc) {
                if (def.elseDefined && macroFunc->hasIf) {
                    callTarget = macroFunc->ifName;
                } else if (def.ifDefined && macroFunc->hasElse) {
                    callTarget = macroFunc->elseName;
                }
            }
            std::string callSelf = "((" + callTarget + "))(" + callArgs + ")";
            std::string ifExpr;
            std::string elseExpr;
            if (def.ifDefined) {
                ifExpr = stripTrailingSemicolon(def.ifBody);
            } else if (def.elseDefined) {
                ifExpr = callSelf;
            }
            if (def.elseDefined) {
                elseExpr = stripTrailingSemicolon(def.elseBody);
            } else if (def.ifDefined) {
                elseExpr = callSelf;
            }
            if (ifExpr.empty()) {
                ifExpr = "0";
            }
            if (elseExpr.empty()) {
                elseExpr = "0";
            }
            out.push_back("#define " + macroSig + " (" + cond + " ? (" + ifExpr +
                          ") : (" + elseExpr + "))\n");
        } else if (guard.hasDefine &&
                   (isSimpleValueMacro(def.ifBody) || isSimpleValueMacro(def.elseBody))) {
            std::string ifExpr = stripTrailingSemicolon(def.ifBody);
            std::string elseExpr = stripTrailingSemicolon(def.elseBody);
            if (ifExpr.empty()) {
                ifExpr = "0";
            }
            if (elseExpr.empty()) {
                elseExpr = "0";
            }
            out.push_back("#define " + macroSig + " (" + cond + " ? (" + ifExpr +
                          ") : (" + elseExpr + "))\n");
        } else if (guard.hasDefine) {
            std::string ifBody = ensureSemicolon(def.ifBody);
            std::string elseBody = ensureSemicolon(def.elseBody);
            out.push_back("#define " + macroSig + " do { if (" + cond + ") { " +
                          ifBody + " } else { " + elseBody + " } } while(0)\n");
        }
    };

    bool pcCommentEmitted = false;
    if (!guard.name.empty()) {
        MacroGuard::MacroDefine primary;
        primary.name = guard.name;
        primary.params = guard.params;
        primary.ifBody = guard.ifBody;
        primary.elseBody = guard.elseBody;
        primary.functionLike = guard.functionLike;
        primary.ifDefined = guard.ifDefined;
        primary.elseDefined = guard.elseDefined;
        emitDefine(primary, pcCommentEmitted);
    }
    for (const auto &def : guard.extraDefines) {
        emitDefine(def, pcCommentEmitted);
    }

    for (const auto &func : guard.functions) {
        if (!func.hasIf && !func.hasElse) {
            continue;
        }
        if (guard.functionLike && guard.hasDefine && func.name == guard.name) {
            continue;
        }
        std::vector<std::string> argNames;
        std::string wrapperParams = buildWrapperParamList(func.params, argNames);
        std::string args;
        for (size_t i = 0; i < argNames.size(); ++i) {
            if (i > 0) {
                args += ", ";
            }
            args += argNames[i];
        }
        std::string signature = func.prefix + func.name + "(" + wrapperParams + ")";
        out.push_back(signature + " {\n");
        bool returnsVoid = isVoidReturnPrefix(func.prefix);
        if (returnsVoid) {
            if (func.hasElse) {
                out.push_back("  if (" + cond + ") { " + func.ifName + "(" + args +
                              "); } else { " + func.elseName + "(" + args + "); }\n");
            } else {
                out.push_back("  " + func.ifName + "(" + args + ");\n");
            }
        } else {
            if (func.hasElse) {
                out.push_back(
                    "  if (" + cond + ") { return " + func.ifName + "(" + args + "); }\n");
                out.push_back("  return " + func.elseName + "(" + args + ");\n");
            } else {
                out.push_back("  return " + func.ifName + "(" + args + ");\n");
            }
        }
        out.push_back("}\n");
    }
}

bool LoweredRenderer::isPreprocessorLine(const std::string &line) {
    size_t pos = 0;
    while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos]))) {
        ++pos;
    }
    return pos < line.size() && line[pos] == '#';
}

bool LoweredRenderer::isLikelyFuncStart(const std::string &line) {
    size_t pos = 0;
    while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos]))) {
        ++pos;
    }
    if (pos >= line.size()) {
        return false;
    }
    if (line[pos] == '#' || line.compare(pos, 2, "//") == 0 ||
        line.compare(pos, 2, "/*") == 0 || line[pos] == '*') {
        return false;
    }
    if (line.find("/*") != std::string::npos || line.find("*/") != std::string::npos) {
        return false;
    }
    static const std::vector<std::string> kKeywords = {
        "if", "for", "while", "switch", "return", "goto", "do", "case"};
    for (const auto &kw : kKeywords) {
        if (line.compare(pos, kw.size(), kw) == 0 &&
            (pos + kw.size() == line.size() ||
             std::isspace(static_cast<unsigned char>(line[pos + kw.size()])) ||
             line[pos + kw.size()] == '(')) {
            return false;
        }
    }
    if (line.find('(') == std::string::npos) {
        return false;
    }
    if (line.find('=') != std::string::npos) {
        return false;
    }
    return true;
}

} // namespace pclower
