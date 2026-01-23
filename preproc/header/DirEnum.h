#pragma once

#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendPluginRegistry.h>
#include <clang/Lex/PPCallbacks.h>
#include <clang/Lex/Preprocessor.h>

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace pclower {

enum class DirEnum { Ifdef, Ifndef, Else, Endif };

struct Directive {
    DirEnum kind;
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
    bool hasDefine = false;
    std::optional<size_t> ifCommentIndex;
    std::optional<size_t> elseCommentIndex;
};
} // namespace pclower