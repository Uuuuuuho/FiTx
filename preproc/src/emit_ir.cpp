#include <clang/CodeGen/CodeGenAction.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/CompilerInvocation.h>
#include <clang/Frontend/TextDiagnosticPrinter.h>
#include <clang/Basic/DiagnosticOptions.h>
#include <clang/Basic/FileManager.h>
#include <clang/Basic/TargetInfo.h>
#include <clang/Basic/TargetOptions.h>
#include <clang/Lex/PreprocessorOptions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/ADT/IntrusiveRefCntPtr.h>
#include <llvm/Support/raw_ostream.h>

#include <memory>
#include <string>
#include <vector>

using namespace clang;
static llvm::cl::opt<std::string> InputPath(llvm::cl::Positional,
                                            llvm::cl::desc("<input.c>"),
                                            llvm::cl::Required);
static llvm::cl::opt<std::string> OutputPath(
    "output", llvm::cl::desc("Output LLVM IR (.ll)"),
    llvm::cl::value_desc("path"), llvm::cl::Required);
static llvm::cl::opt<std::string> ResourceDir(
    "resource-dir", llvm::cl::desc("Clang resource dir"),
    llvm::cl::value_desc("path"));

static llvm::cl::list<std::string> Defines(
    "D", llvm::cl::desc("Preprocessor defines"), llvm::cl::ZeroOrMore,
    llvm::cl::Prefix);

int main(int argc, char **argv) {
    llvm::cl::ParseCommandLineOptions(argc, argv, "Emit LLVM IR with Clang\n");

    std::vector<std::string> args = {"-x", "c", "-std=c11"};
    for (const auto &def : Defines) {
        args.push_back("-D" + def);
    }
    if (!ResourceDir.empty()) {
        args.push_back("-resource-dir");
        args.push_back(ResourceDir);
        std::string resourceInclude = ResourceDir + "/include";
        if (llvm::sys::fs::is_directory(resourceInclude)) {
            args.push_back("-isystem");
            args.push_back(resourceInclude);
        }
    }
    const char *includeDirs[] = {"/usr/include", "/usr/local/include", "/usr/include/x86_64-linux-gnu"};
    for (const auto *dir : includeDirs) {
        if (llvm::sys::fs::is_directory(dir)) {
            args.push_back("-isystem");
            args.push_back(dir);
        }
    }
    args.push_back(InputPath);

    std::vector<const char *> cargs;
    cargs.reserve(args.size());
    for (const auto &arg : args) {
        cargs.push_back(arg.c_str());
    }

    IntrusiveRefCntPtr<DiagnosticOptions> diagOpts(new DiagnosticOptions());
    auto diagPrinter = std::make_unique<TextDiagnosticPrinter>(llvm::errs(), diagOpts.get());
    IntrusiveRefCntPtr<DiagnosticIDs> diagIDs(new DiagnosticIDs());
    DiagnosticsEngine diagnostics(diagIDs, diagOpts.get(), diagPrinter.get(), false);

    auto invocation = std::make_shared<CompilerInvocation>();
    CompilerInvocation::CreateFromArgs(*invocation, cargs, diagnostics);

    CompilerInstance ci;
    ci.setInvocation(invocation);
    ci.createDiagnostics(diagPrinter.release(), false);

    auto targetOpts = ci.getInvocation().TargetOpts;
    ci.setTarget(TargetInfo::CreateTargetInfo(ci.getDiagnostics(), targetOpts));
    ci.createFileManager();
    ci.createSourceManager(ci.getFileManager());
    if (!ResourceDir.empty()) {
        ci.getHeaderSearchOpts().ResourceDir = ResourceDir;
    }
    ci.getHeaderSearchOpts().UseBuiltinIncludes = true;
    ci.getHeaderSearchOpts().UseStandardSystemIncludes = true;
    ci.getHeaderSearchOpts().UseStandardCXXIncludes = false;
    ci.createPreprocessor(TU_Complete);
    ci.createASTContext();

    auto fileRefOrErr = ci.getFileManager().getFileRef(InputPath);
    if (!fileRefOrErr) {
        llvm::errs() << "Failed to read input file: " << InputPath << "\n";
        return 1;
    }
    FileEntryRef fileRef = *fileRefOrErr;
    ci.getSourceManager().setMainFileID(
        ci.getSourceManager().createFileID(fileRef, SourceLocation(), SrcMgr::C_User));
    ci.getDiagnosticClient().BeginSourceFile(ci.getLangOpts(), &ci.getPreprocessor());

    llvm::LLVMContext context;
    EmitLLVMOnlyAction action(&context);
    if (!ci.ExecuteAction(action)) {
        llvm::errs() << "Failed to emit LLVM IR\n";
        return 1;
    }

    std::unique_ptr<llvm::Module> module = action.takeModule();
    if (!module) {
        llvm::errs() << "No LLVM module produced\n";
        return 1;
    }

    std::error_code ec;
    llvm::raw_fd_ostream outFile(OutputPath, ec, llvm::sys::fs::OF_Text);
    if (ec) {
        llvm::errs() << "Failed to write IR: " << ec.message() << "\n";
        return 1;
    }
    module->print(outFile, nullptr);
    ci.getDiagnosticClient().EndSourceFile();
    return 0;
}
