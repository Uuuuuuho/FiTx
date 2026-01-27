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
    struct GuardedFunction {
        std::string name;
        std::string prefix;
        std::string params;
        std::string ifName;
        std::string elseName;
        bool hasIf = false;
        bool hasElse = false;
        bool protoEmitted = false;
    };
    struct MacroDefine {
        std::string name;
        std::string params;
        std::string ifBody;
        std::string elseBody;
        bool functionLike = false;
        bool ifDefined = false;
        bool elseDefined = false;
    };
    std::string macro;
    bool negated = false;
    std::string name;
    std::string params;
    std::string ifBody;
    std::string elseBody;
    bool inElse = false;
    bool hasDefine = false;
    bool functionLike = false;
    bool ifDefined = false;
    bool elseDefined = false;
    std::optional<size_t> ifCommentIndex;
    std::optional<size_t> elseCommentIndex;
    std::vector<GuardedFunction> functions;
    std::vector<MacroDefine> extraDefines;
};
} // namespace pclower