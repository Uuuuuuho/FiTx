#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "DirEnum.h"

namespace pclower {

class LoweredRenderer {
  public:
    LoweredRenderer(const std::vector<std::string> &lines,
                    const std::map<unsigned, Directive> &directiveMap);

    std::vector<std::string> render();

  private:
    const std::vector<std::string> &lines_;
    const std::map<unsigned, Directive> &directiveMap_;

    std::vector<std::string> out_;
    std::vector<std::string> stack_;
    std::vector<std::string> modeStack_;
    int indentLevel_ = 0;
    int braceDepth_ = 0;
    std::optional<int> structDepth_;
    std::optional<MacroGuard> macroGuard_;
    bool collectingFunc_ = false;
    bool abortCollect_ = false;
    std::vector<std::string> funcLines_;

    void handleDirective(DirEnum kind, const std::string &macro);
    void updateStructDepthForLine(const std::string &line);
    bool tryHandleCollectedFuncSignature();
    void flushCollectedFuncLines();

    static std::string pcExpr(const std::vector<std::string> &stack);
    static std::string invert(const std::string &expr);
    static void emitHelper(std::vector<std::string> &out);
    static std::string indent(int level);
    static bool inStruct(const std::optional<int> &structDepth, int braceDepth);
    static bool inTopLevel(const std::optional<int> &structDepth, int braceDepth);
    static int countChar(const std::string &line, char c);
    static std::string rtrim(const std::string &s);
    static std::string stripTrailingSemicolon(const std::string &body);
    static std::string buildCallArgs(const std::string &params);
    static bool isSimpleValueMacro(const std::string &body);
    static bool extractFunctionSignature(const std::string &line,
                                         std::string &prefix,
                                         std::string &name,
                                         std::string &params,
                                         std::string &suffix);
    static std::string buildWrapperParamList(const std::string &params,
                                             std::vector<std::string> &argNames);
    static bool isVoidReturnPrefix(const std::string &prefix);
    static bool handleMacroDefine(const std::string &line,
                                  MacroGuard &guard,
                                  std::vector<std::string> &out);
    static std::string ensureSemicolon(const std::string &body);
    static void emitMacroGuard(std::vector<std::string> &out,
                               const MacroGuard &guard);
    static bool isPreprocessorLine(const std::string &line);
    static bool isLikelyFuncStart(const std::string &line);
    static bool parseRawDirective(const std::string &line,
                                  DirEnum &kind,
                                  std::string &macro);
};

} // namespace pclower
