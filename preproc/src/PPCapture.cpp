#include "PPCapture.h"

namespace pclower {

PPCapturer::PPCapturer(clang::SourceManager &sm, std::vector<Directive> &out)
    : sm_(sm), directives_(out) {}

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