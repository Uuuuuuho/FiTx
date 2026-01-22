#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/raw_ostream.h>

#include <set>
#include <string>
#include <vector>

using namespace llvm;

static cl::opt<std::string> InputIR(cl::Positional, cl::desc("<input.ll>"),
                                    cl::Required);

int main(int argc, char **argv) {
    cl::ParseCommandLineOptions(argc, argv, "LLVM IR summary\n");

    LLVMContext context;
    SMDiagnostic err;
    std::unique_ptr<Module> module = parseIRFile(InputIR, err, context);
    if (!module) {
        err.print(argv[0], errs());
        return 1;
    }

    std::set<std::string> functions;
    std::set<std::string> structs;
    std::set<std::string> calls;
    size_t annotations = 0;

    for (const auto &func : module->functions()) {
        if (!func.isDeclaration()) {
            functions.insert(func.getName().str());
        }
        for (const auto &bb : func) {
            for (const auto &inst : bb) {
                if (const auto *call = dyn_cast<CallBase>(&inst)) {
                    if (const Function *callee = call->getCalledFunction()) {
                        calls.insert(callee->getName().str());
                    }
                }
            }
        }
    }

    for (const auto &st : module->getIdentifiedStructTypes()) {
        if (st->hasName()) {
            structs.insert(st->getName().str());
        }
    }

    if (module->getFunction("llvm.var.annotation")) {
        for (const auto &func : module->functions()) {
            for (const auto &bb : func) {
                for (const auto &inst : bb) {
                    if (const auto *call = dyn_cast<CallBase>(&inst)) {
                        if (const Function *callee = call->getCalledFunction()) {
                            if (callee->getName().starts_with("llvm.var.annotation")) {
                                annotations++;
                            }
                        }
                    }
                }
            }
        }
    }

    outs() << "IR: " << InputIR << "\n";
    outs() << "  functions: ";
    if (functions.empty()) {
        outs() << "none\n";
    } else {
        bool first = true;
        for (const auto &name : functions) {
            if (!first) {
                outs() << ", ";
            }
            outs() << name;
            first = false;
        }
        outs() << "\n";
    }

    outs() << "  structs: ";
    if (structs.empty()) {
        outs() << "none\n";
    } else {
        bool first = true;
        for (const auto &name : structs) {
            if (!first) {
                outs() << ", ";
            }
            outs() << name;
            first = false;
        }
        outs() << "\n";
    }

    outs() << "  calls: ";
    if (calls.empty()) {
        outs() << "none\n";
    } else {
        bool first = true;
        for (const auto &name : calls) {
            if (!first) {
                outs() << ", ";
            }
            outs() << name;
            first = false;
        }
        outs() << "\n";
    }

    outs() << "  annotations: " << annotations << "\n";
    return 0;
}
