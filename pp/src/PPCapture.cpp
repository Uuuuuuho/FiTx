#include "PPCapture.h"

#include <clang/Lex/Lexer.h>

#include <cctype>
#include <regex>

namespace pclower {

PPCapturer::PPCapturer(clang::SourceManager &sm,
                       const clang::LangOptions &langOpts,
                       std::vector<Directive> &out)
    : sm_(sm), langOpts_(langOpts), directives_(out) {}

static std::optional<std::pair<std::string, bool>> parseIfMacro(
    clang::SourceManager &sm,
    const clang::LangOptions &langOpts,
    clang::SourceRange conditionRange) {
    auto text = clang::Lexer::getSourceText(
        clang::CharSourceRange::getTokenRange(conditionRange), sm, langOpts);
    if (text.empty()) {
        return std::nullopt;
    }
    std::string cond = text.str();
    static const std::regex macroRe(R"(CONFIG_[A-Za-z0-9_]+)");
    std::smatch match;
    if (!std::regex_search(cond, match, macroRe)) {
        return std::nullopt;
    }
    std::string macro = match[0].str();
    bool negated = false;
    size_t pos = cond.find(macro);
    if (pos != std::string::npos) {
        size_t i = pos;
        while (i > 0 && std::isspace(static_cast<unsigned char>(cond[i - 1]))) {
            --i;
        }
        if (i > 0 && cond[i - 1] == '!') {
            negated = true;
        }
    }
    if (cond.find("!defined") != std::string::npos ||
        cond.find("! IS_ENABLED") != std::string::npos ||
        cond.find("!IS_ENABLED") != std::string::npos) {
        negated = true;
    }
    return std::make_pair(macro, negated);
}

void PPCapturer::Ifdef(clang::SourceLocation loc,
                       const clang::Token &macroNameTok,
                       const clang::MacroDefinition &) {
    if (!sm_.isWrittenInMainFile(loc)) {
        return;
    }
    directives_.push_back({DirEnum::Ifdef,
                           macroNameTok.getIdentifierInfo()->getName().str(),
                           sm_.getSpellingLineNumber(loc)});
}

void PPCapturer::Ifndef(clang::SourceLocation loc,
                        const clang::Token &macroNameTok,
                        const clang::MacroDefinition &) {
    if (!sm_.isWrittenInMainFile(loc)) {
        return;
    }
    directives_.push_back({DirEnum::Ifndef,
                           macroNameTok.getIdentifierInfo()->getName().str(),
                           sm_.getSpellingLineNumber(loc)});
}

void PPCapturer::If(clang::SourceLocation loc,
                    clang::SourceRange conditionRange,
                    clang::PPCallbacks::ConditionValueKind) {
    if (!sm_.isWrittenInMainFile(loc)) {
        return;
    }
    auto parsed = parseIfMacro(sm_, langOpts_, conditionRange);
    if (!parsed) {
        return;
    }
    const auto &macro = parsed->first;
    bool negated = parsed->second;
    directives_.push_back({negated ? DirEnum::Ifndef : DirEnum::Ifdef,
                           macro,
                           sm_.getSpellingLineNumber(loc)});
}

void PPCapturer::Elif(clang::SourceLocation loc,
                      clang::SourceRange conditionRange,
                      clang::PPCallbacks::ConditionValueKind,
                      clang::SourceLocation) {
    if (!sm_.isWrittenInMainFile(loc)) {
        return;
    }
    auto parsed = parseIfMacro(sm_, langOpts_, conditionRange);
    if (!parsed) {
        return;
    }
    directives_.push_back({DirEnum::Else, "", sm_.getSpellingLineNumber(loc)});
    const auto &macro = parsed->first;
    bool negated = parsed->second;
    directives_.push_back({negated ? DirEnum::Ifndef : DirEnum::Ifdef,
                           macro,
                           sm_.getSpellingLineNumber(loc)});
}

void PPCapturer::Else(clang::SourceLocation loc, clang::SourceLocation) {
    if (!sm_.isWrittenInMainFile(loc)) {
        return;
    }
    directives_.push_back({DirEnum::Else, "", sm_.getSpellingLineNumber(loc)});
}

void PPCapturer::Endif(clang::SourceLocation loc, clang::SourceLocation) {
    if (!sm_.isWrittenInMainFile(loc)) {
        return;
    }
    directives_.push_back({DirEnum::Endif, "", sm_.getSpellingLineNumber(loc)});
}

} // namespace pclower