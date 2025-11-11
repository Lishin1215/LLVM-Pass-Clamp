
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include <optional>
#include <cmath>
#include <cstdlib>
#include <string>
#include <algorithm>

using namespace llvm;

// ============================================================================
// [Part 1] ClampOpt — select → llvm.smax / llvm.umin helper
// ============================================================================
namespace {

enum class ClampKind { None, Min, Max };

// 🔹 專門給整數比較用的「小於」與「大於」判斷
// 整數比較：小於/大於
static bool isIntLessPredicate(ICmpInst::Predicate Pred) {
    switch (Pred) {
    case ICmpInst::ICMP_SLT:
    case ICmpInst::ICMP_SLE:
    case ICmpInst::ICMP_ULT:
    case ICmpInst::ICMP_ULE:
        return true;
    default:
        return false;
    }
}

static bool isIntGreaterPredicate(ICmpInst::Predicate Pred) {
    switch (Pred) {
    case ICmpInst::ICMP_SGT:
    case ICmpInst::ICMP_SGE:
    case ICmpInst::ICMP_UGT:
    case ICmpInst::ICMP_UGE:
        return true;
    default:
        return false;
    }
}

// 浮點比較：小於/大於
static bool isFloatLessPredicate(FCmpInst::Predicate Pred) {
    switch (Pred) {
    case FCmpInst::FCMP_OLT:
    case FCmpInst::FCMP_OLE:
    case FCmpInst::FCMP_ULT:
    case FCmpInst::FCMP_ULE:
        return true;
    default:
        return false;
    }
}

static bool isFloatGreaterPredicate(FCmpInst::Predicate Pred) {
    switch (Pred) {
    case FCmpInst::FCMP_OGT:
    case FCmpInst::FCMP_OGE:
    case FCmpInst::FCMP_UGT:
    case FCmpInst::FCMP_UGE:
        return true;
    default:
        return false;
    }
}


// 🔹 Helper: detect nested select clamp pattern (libpng style)
// Pattern: select (cmp1) ? (select (cmp2) ? var : upper) : lower
//   → clamp(var, lower, upper)
static bool rewriteNestedSelectClamp(SelectInst &SI) {
    Value *OuterCond = SI.getCondition();
    Value *OuterTrue = SI.getTrueValue();
    Value *OuterFalse = SI.getFalseValue();

    // Check if TrueVal is another select
    auto *InnerSel = dyn_cast<SelectInst>(OuterTrue);
    if (!InnerSel)
        return false;

    Value *InnerTrue = InnerSel->getTrueValue();
    Value *InnerFalse = InnerSel->getFalseValue();

    // We expect: outer select picks between (inner_select) and lower_bound
    // Inner select picks between var and upper_bound
    // Pattern: select (var >= lower) ? (select (var <= upper) ? var : upper) : lower

    // Try to identify: InnerTrue == variable, InnerFalse == upper, OuterFalse == lower
    Value *Var = InnerTrue;
    Value *Upper = InnerFalse;
    Value *Lower = OuterFalse;

    Type *Ty = SI.getType();
    if (Var->getType() != Ty || Upper->getType() != Ty || Lower->getType() != Ty)
        return false;

    // Check if conditions reference the variable and bounds
    // This is a simplified heuristic; real pattern matching would be more robust
    auto hasCmpWith = [](Value *Cond, Value *Val) -> bool {
        if (auto *Cmp = dyn_cast<CmpInst>(Cond)) {
            return Cmp->getOperand(0) == Val || Cmp->getOperand(1) == Val;
        }
        if (auto *BinOp = dyn_cast<BinaryOperator>(Cond)) {
            // Handle 'and' of two comparisons
            if (BinOp->getOpcode() == Instruction::And) {
                for (unsigned i = 0; i < 2; ++i) {
                    if (auto *Cmp = dyn_cast<CmpInst>(BinOp->getOperand(i))) {
                        if (Cmp->getOperand(0) == Val || Cmp->getOperand(1) == Val)
                            return true;
                    }
                }
            }
        }
        return false;
    };

    if (!hasCmpWith(OuterCond, Var) || !hasCmpWith(InnerSel->getCondition(), Var))
        return false;

    // Determine type: float or integer
    Type *ScalarTy = Ty->getScalarType();
    Intrinsic::ID MaxID = Intrinsic::not_intrinsic;
    Intrinsic::ID MinID = Intrinsic::not_intrinsic;

    if (ScalarTy->isFloatingPointTy()) {
        MaxID = Intrinsic::maxnum;
        MinID = Intrinsic::minnum;
    } else if (ScalarTy->isIntegerTy()) {
        // Assume unsigned for now (could enhance with predicate analysis)
        MaxID = Intrinsic::umax;
        MinID = Intrinsic::umin;
    } else {
        return false;
    }

    IRBuilder<> Builder(&SI);
    Value *Clamped1 = Builder.CreateBinaryIntrinsic(MaxID, Var, Lower, nullptr, "clamp.lower");
    Value *Clamped2 = Builder.CreateBinaryIntrinsic(MinID, Clamped1, Upper, nullptr, "clamp.upper");

    SI.replaceAllUsesWith(Clamped2);
    SI.eraseFromParent();

    // Clean up dead instructions
    if (InnerSel->use_empty())
        InnerSel->eraseFromParent();

    return true;
}

// 🔹 原本 ClampOptPass::rewriteSelect
static bool rewriteSelect(SelectInst &SI) {
    // Try nested select pattern first
    if (rewriteNestedSelectClamp(SI))
        return true;

    auto *Cmp = dyn_cast<CmpInst>(SI.getCondition());
    if (!Cmp)
        return false;

    Value *TrueVal = SI.getTrueValue();
    Value *FalseVal = SI.getFalseValue();
    Value *LHS = Cmp->getOperand(0);
    Value *RHS = Cmp->getOperand(1);

    Type *SelTy = SI.getType();
    Type *ScalarTy = SelTy->getScalarType();
    if (!ScalarTy->isIntegerTy() && !ScalarTy->isFloatingPointTy())
        return false;

    ClampKind DesiredKind = ClampKind::None;
    bool IsLess = false;
    bool IsGreater = false;
    bool IsSigned = false;
    bool IsUnsigned = false;
    bool IsFloat = isa<FCmpInst>(Cmp);

    if (auto *IC = dyn_cast<ICmpInst>(Cmp)) {
        auto Pred = IC->getPredicate();
        IsSigned = ICmpInst::isSigned(Pred);
        IsUnsigned = ICmpInst::isUnsigned(Pred);
        IsLess = isIntLessPredicate(Pred);
        IsGreater = isIntGreaterPredicate(Pred);
    } else if (auto *FC = dyn_cast<FCmpInst>(Cmp)) {
        auto Pred = FC->getPredicate();
        IsLess = isFloatLessPredicate(Pred);
        IsGreater = isFloatGreaterPredicate(Pred);
    }



    if ((!IsLess && !IsGreater) || (!IsFloat && !IsSigned && !IsUnsigned))
        return false;

    ClampKind Kind = ClampKind::None;
    Value *A = LHS, *B = RHS;

    if (IsLess) {
        if (TrueVal == B && FalseVal == A)
            Kind = ClampKind::Max;
        else if (TrueVal == A && FalseVal == B)
            Kind = ClampKind::Min;
    } else if (IsGreater) {
        if (TrueVal == A && FalseVal == B)
            Kind = ClampKind::Max;
        else if (TrueVal == B && FalseVal == A)
            Kind = ClampKind::Min;
    }

    if (Kind == ClampKind::None)
        return false;

    Intrinsic::ID IID = Intrinsic::not_intrinsic;
    if (IsFloat)
        IID = (Kind == ClampKind::Max) ? Intrinsic::maxnum : Intrinsic::minnum;
    else if (IsSigned)
        IID = (Kind == ClampKind::Max) ? Intrinsic::smax : Intrinsic::smin;
    else if (IsUnsigned)
        IID = (Kind == ClampKind::Max) ? Intrinsic::umax : Intrinsic::umin;
    else
        return false;

    IRBuilder<> Builder(&SI);
    Value *NewVal = Builder.CreateBinaryIntrinsic(IID, A, B, nullptr,
        (Kind == ClampKind::Max) ? "clamp.max" : "clamp.min");
    SI.replaceAllUsesWith(NewVal);
    SI.eraseFromParent();
    
    // 🔹 刪除無用的比較指令（如果沒人用了）
    if (Cmp->use_empty()) {
        Cmp->eraseFromParent();
    }
    
    return true;
}


static bool optimizeClamp(Function &F) {
    SmallVector<SelectInst *, 16> Worklist;
    for (auto &BB : F)
        for (auto &I : BB)
            if (auto *SI = dyn_cast<SelectInst>(&I))
                Worklist.push_back(SI);

    bool Changed = false;
    unsigned Count = 0;
    for (SelectInst *SI : Worklist)
        if (SI && rewriteSelect(*SI)) {
            Changed = true;
            ++Count;
        }

    if (Changed)
        errs() << "[ClampOpt] " << F.getName() << ": rewrote " << Count
               << " clamp selects\n";
    return Changed;
}

} // end anonymous namespace

// ============================================================================
// [Part 2] MathOpt — 原 ImgOptPass 數學優化邏輯
// ============================================================================

static Value *buildPowerApprox(IRBuilder<> &B, Function &F, Value *X, double ExpVal) {
    Type *Ty = X->getType();
    auto *Sqrt = Intrinsic::getDeclaration(F.getParent(), Intrinsic::sqrt, {Ty});

    auto sqrtN = [&](Value *V, int n) -> Value * {
        Value *Tmp = V;
        for (int i = 0; i < n; ++i)
            Tmp = B.CreateCall(Sqrt, {Tmp});
        return Tmp;
    };

    // 🔹 CIE Lab / XYZ：立方根 pow(x, 1/3)
    if (fabs(ExpVal - 1.0/3.0) < 0.01) {
        errs() << "    → Optimizing pow(x, 1/3) to cube root approximation\n";
        Value *s1 = B.CreateCall(Sqrt, {X});
        Value *s2 = B.CreateCall(Sqrt, {s1});
        Value *s3 = B.CreateCall(Sqrt, {s2});
        return B.CreateFMul(s3, s3);
    }

    // 🔹 1.2 次方與 gamma 編解碼常見近似
    if (fabs(ExpVal - 1.2) < 1e-3) {
        Value *s1 = B.CreateCall(Sqrt, {X});
        Value *s2 = B.CreateCall(Sqrt, {s1});
        Value *s3 = B.CreateCall(Sqrt, {s2});
        return B.CreateFMul(X, s3);
    }

    // 🔹 pow(x,2.2) / pow(x,2.4) / pow(x,1.8)
    if (fabs(ExpVal - 2.2) < 0.05)
        return B.CreateFMul(B.CreateFMul(X, X), sqrtN(X, 3));
    if (fabs(ExpVal - 2.4) < 0.05)
        return B.CreateFMul(B.CreateFMul(X, X), sqrtN(X, 2));
    if (fabs(ExpVal - 1.8) < 0.05)
        return B.CreateFMul(B.CreateFMul(X, X), sqrtN(X, 1));

    // 🔹 pow(x,1/2.2) / pow(x,1/2.4) / pow(x,1/1.8)
    if (fabs(ExpVal - 1.0/2.2) < 0.05)
        return B.CreateFMul(sqrtN(X, 3), B.CreateCall(Sqrt, {X}));
    if (fabs(ExpVal - 1.0/2.4) < 0.05)
        return sqrtN(X, 3);
    if (fabs(ExpVal - 1.0/1.8) < 0.05)
        return B.CreateFMul(B.CreateCall(Sqrt, {X}), sqrtN(X, 2));

    return nullptr;
}

static bool optimizeMathFunctions(Function &F) {
    bool Changed = false;
    SmallVector<std::pair<Instruction *, Value *>, 8> Replacements;
    IRBuilder<> Builder(F.getContext());

    errs() << "[MathOpt] Scanning for pow/gamma optimizations\n";

    for (auto &BB : F)
        for (auto &I : BB) {
            auto *CI = dyn_cast<CallInst>(&I);
            if (!CI) continue;

            Function *Callee = CI->getCalledFunction();
            if (!Callee) continue;

            StringRef Name = Callee->getName();
            if (!(Name.contains("pow") || Name.contains("powf") ||
                  Name.contains("llvm.pow") || Name.contains("cv::pow")))
                continue;

            if (CI->arg_size() != 2) continue;

            Builder.SetInsertPoint(&I);
            Value *X = CI->getOperand(0);
            Value *Exponent = CI->getOperand(1);

            if (auto *Cast = dyn_cast<FPExtInst>(Exponent))
                Exponent = Cast->getOperand(0);

            if (auto *Kc = dyn_cast_or_null<ConstantFP>(Exponent->stripPointerCasts())) {
                double ExpVal = Kc->getValueAPF().convertToDouble();
                if (auto *Approx = buildPowerApprox(Builder, F, X, ExpVal)) {
                    errs() << "  [MathOpt] pow(x," << ExpVal << ") → fast chain\n";
                    Replacements.push_back({&I, Approx});
                    Changed = true;
                    continue;
                }
            }
            if (auto *Ld = dyn_cast<LoadInst>(Exponent)) {
                if (auto *GV = dyn_cast<GlobalVariable>(Ld->getPointerOperand())) {
                    // 先看 initializer 是不是個 ConstantFP（e.g., 1.2 / 2.2 / 1/2.2 …）
                    if (GV->hasInitializer()) {
                        if (auto *InitC = dyn_cast<ConstantFP>(GV->getInitializer())) {
                            double Val = InitC->getValueAPF().convertToDouble();
                            if (auto *Approx = buildPowerApprox(Builder, F, X, Val)) {
                                errs() << "  [MathOpt] pow(x, g=" << Val << ") (global const) → fast chain\n";
                                Replacements.push_back({&I, Approx});
                                Changed = true;
                                continue;
                            }
                        }
                    }
                }
            }
        }

    for (auto &P : Replacements) {
        Instruction *Old = P.first;
        Value *NewV = P.second;
        Old->replaceAllUsesWith(NewV);
        Old->eraseFromParent();
    }

    errs() << (Changed ? "[MathOpt] Applied math optimizations\n"
                       : "[MathOpt] No math optimizations found\n");
    return Changed;
}

// ============================================================================
// [Part 3] Main ImgOptPass — 同時呼叫 Clamp + MathOpt
// ============================================================================
struct ImgOptPass : public PassInfoMixin<ImgOptPass> {
    PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM) {
        errs() << "<<< [ImgOptPass] Processing function: " << F.getName() << " >>>\n";
        bool Changed = false;
        Changed |= optimizeClamp(F);          // 加入 clamp pattern 優化
        // Changed |=  optimizeMathFunctions(F);  // 原本數學優化
        if (Changed) {
            errs() << "[ImgOptPass] Changes applied\n";
            return PreservedAnalyses::none();
        }
        errs() << "[ImgOptPass] No changes\n";
        return PreservedAnalyses::all();
    }
};

// ============================================================================
// [註冊插件]
// ============================================================================
extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
    return {
        LLVM_PLUGIN_API_VERSION, "STB Image Optimization Pass", "1.0.0",
        [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, FunctionPassManager &FPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                    if (Name == "img-opt") {
                        FPM.addPass(ImgOptPass());
                        return true;
                    }
                    return false;
                });
        }
    };
}
