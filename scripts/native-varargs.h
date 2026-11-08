// Comparison-private x86-64 System V variadic storage. Keep real calls and
// va_start: the backend owns register assignment, stack alignment and padding.
// A probe with the call's arguments named asks that SAME backend for its
// incoming stack extent. Subtract the named-only extent to bound overflow
// arguments without guessing ABI classes from LLVM types.
#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/TargetParser/Triple.h"
using namespace llvm;

class NativeVarargs {
  Module &M;
  Type *P, *I64;
  GlobalVariable *extent, *probeResult;
  DenseMap<Function *, Value *> incoming;
  unsigned serial=0;

  // SysV unnamed vectors wider than XMM are stack arguments even when named
  // vectors use AVX registers. A byval probe parameter preserves that row.
  bool stackVector(CallInst *call, unsigned index, unsigned fixed) {
    Type *T=call->getArgOperand(index)->getType();
    return index>=fixed && T->isVectorTy() && M.getDataLayout().getTypeAllocSize(T)>16;
  }

  AttributeSet returnABI(CallInst *call) {
    AttrBuilder attrs(M.getContext());
    for (auto kind:{Attribute::SExt,Attribute::ZExt,Attribute::InReg})
      if (call->getAttributes().hasRetAttr(kind)) attrs.addAttribute(kind);
    return AttributeSet::get(M.getContext(),attrs);
  }

  Value *probe(CallInst *call, unsigned count, IRBuilder<> &B) {
    unsigned fixed=call->getFunctionType()->getNumParams();
    SmallVector<Type *> types;
    SmallVector<Value *> values;
    SmallVector<AttributeSet> attrs;
    for (unsigned i=0;i<count;++i) {
      Value *value=call->getArgOperand(i);
      AttributeSet attr=call->getAttributes().getParamAttrs(i)
          .removeAttribute(M.getContext(),Attribute::Returned);
      if (stackVector(call,i,fixed)) {
        Type *T=value->getType();
        auto align=M.getDataLayout().getABITypeAlign(T);
        auto *slot=B.CreateAlloca(T,nullptr,"native.va.vector"); slot->setAlignment(align);
        B.CreateAlignedStore(value,slot,align); value=slot;
        AttrBuilder byval(M.getContext()); byval.addByValAttr(T); byval.addAlignmentAttr(align);
        attr=AttributeSet::get(M.getContext(),byval);
      }
      types.push_back(value->getType()); values.push_back(value); attrs.push_back(attr);
    }
    auto *T=FunctionType::get(call->getType(),types,true);
    auto *F=Function::Create(T,GlobalValue::InternalLinkage,
                            "__bennett_native_va_probe."+Twine(serial++),M);
    F->setCallingConv(call->getCallingConv());
    F->setAttributes(AttributeList::get(M.getContext(),AttributeSet(),
                                      returnABI(call),attrs));
    F->addFnAttr(Attribute::NoInline); F->addFnAttr(Attribute::OptimizeNone);
    for (StringRef key:{"target-cpu","target-features","tune-cpu","use-soft-float"})
      if (call->getFunction()->hasFnAttribute(key))
        F->addFnAttr(call->getFunction()->getFnAttribute(key));
    buildProbe(F);
    auto *invoke=B.CreateCall(F,values); invoke->setCallingConv(F->getCallingConv());
    invoke->setAttributes(AttributeList::get(M.getContext(),AttributeSet(),
                                             returnABI(call),attrs));
    return B.CreateLoad(I64,probeResult,"native.va.extent");
  }

  Function *vaIntrinsic(Intrinsic::ID id) {
    SmallVector<Type *,1> types;
    if (Intrinsic::isOverloaded(id)) types.push_back(P);
    return Intrinsic::getDeclaration(&M,id,types);
  }

  void buildProbe(Function *F) {
    IRBuilder<> B(BasicBlock::Create(M.getContext(),"entry",F));
    // SysV va_list is {u32 gp_offset,u32 fp_offset,ptr overflow,ptr save}.
    auto *ap=B.CreateAlloca(ArrayType::get(B.getInt8Ty(),24)); ap->setAlignment(Align(8));
    B.CreateCall(vaIntrinsic(Intrinsic::vastart),{ap});
    auto *end=B.CreateLoad(P,B.CreateConstGEP1_64(B.getInt8Ty(),ap,8));
    auto *frame=B.CreateCall(Intrinsic::getDeclaration(&M,Intrinsic::frameaddress,{P}),{B.getInt32(0)});
    // The incoming argument area follows the saved frame pointer and return PC.
    auto *base=B.CreateAdd(B.CreatePtrToInt(frame,I64),B.getInt64(16));
    B.CreateStore(B.CreateSub(B.CreatePtrToInt(end,I64),base),probeResult);
    B.CreateCall(vaIntrinsic(Intrinsic::vaend),{ap});
    if (F->getReturnType()->isVoidTy()) B.CreateRetVoid();
    else B.CreateRet(Constant::getNullValue(F->getReturnType()));
  }

  void callExtent(CallInst *call) {
    IRBuilder<> B(call);
    auto *previous=B.CreateLoad(I64,extent,"native.va.previous");
    auto *named=probe(call,call->getFunctionType()->getNumParams(),B);
    auto *all=probe(call,call->arg_size(),B);
    auto *invalid=B.CreateICmpULT(all,named);
    auto *failure=SplitBlockAndInsertIfThen(invalid,call,true);
    IRBuilder<> error(failure);
    auto infrastructure=M.getOrInsertFunction("__bennett_rt_native_infrastructure",error.getVoidTy(),P);
    error.CreateCall(infrastructure,{error.CreateGlobalStringPtr("invalid native argument extent")});
    B.SetInsertPoint(call); B.CreateStore(B.CreateSub(all,named),extent);
    IRBuilder<> after(call->getNextNode()); after.CreateStore(previous,extent);
  }

  uint64_t saveBytes(Function &F) {
    StringRef cpu=F.hasFnAttribute("target-cpu")?F.getFnAttribute("target-cpu").getValueAsString():"x86-64";
    std::string features=F.hasFnAttribute("target-features")?
        F.getFnAttribute("target-features").getValueAsString().str():"";
    if (F.getFnAttribute("use-soft-float").getValueAsBool()) features="+soft-float,"+features;
    std::string error;
    const Target *target=TargetRegistry::lookupTarget(M.getTargetTriple(),error);
    if (!target) report_fatal_error(Twine("native variadic target: ")+error);
    std::unique_ptr<MCSubtargetInfo> subtarget(
        target->createMCSubtargetInfo(M.getTargetTriple(),cpu,features));
    if (!subtarget) report_fatal_error("native variadic target has no feature information");
    // LLVM allocates six GP slots and, when enabled, eight XMM slots. Ask
    // its feature parser so feature implications and soft-float agree too.
    return 48+(subtarget->checkFeatures("+sse,-soft-float")?128:0);
  }

  void registerList(IntrinsicInst *start, FunctionCallee add) {
    IRBuilder<> B(start->getNextNode());
    auto *ap=start->getArgOperand(0);
    auto field=[&](unsigned offset) { return B.CreateConstGEP1_64(B.getInt8Ty(),ap,offset); };
    auto *save=B.CreateLoad(P,field(16));
    B.CreateCall(add,{save,B.getInt64(saveBytes(*start->getFunction()))});
    auto *overflow=B.CreateLoad(P,field(8));
    auto entry=incoming.find(start->getFunction());
    if (entry==incoming.end()) report_fatal_error("va_start outside a native variadic definition");
    B.CreateCall(add,{overflow,entry->second});
  }

public:
  NativeVarargs(Module &module):M(module) {
    auto &C=M.getContext(); P=PointerType::getUnqual(C); I64=Type::getInt64Ty(C);
    extent=new GlobalVariable(M,I64,false,GlobalValue::InternalLinkage,
                              ConstantInt::get(I64,0),"__bennett_native_va_extent");
    probeResult=new GlobalVariable(M,I64,false,GlobalValue::InternalLinkage,
                                   ConstantInt::get(I64,0),"__bennett_native_va_probe_result");
  }

  void instrument(FunctionCallee check, FunctionCallee add) {
    SmallVector<CallInst *> calls;
    SmallVector<IntrinsicInst *> starts, copies;
    for (Function &F:M) {
      if (F.isDeclaration()) continue;
      if (F.isVarArg()) {
        IRBuilder<> B(&*F.getEntryBlock().getFirstInsertionPt());
        incoming[&F]=B.CreateLoad(I64,extent,"native.va.incoming");
      }
      for (Instruction &I:instructions(F)) {
        auto *call=dyn_cast<CallInst>(&I); if (!call) continue;
        auto *intrinsic=dyn_cast<IntrinsicInst>(call);
        if (intrinsic && intrinsic->getIntrinsicID()==Intrinsic::vastart) starts.push_back(intrinsic);
        else if (intrinsic && intrinsic->getIntrinsicID()==Intrinsic::vacopy) copies.push_back(intrinsic);
        else if (!intrinsic && call->getFunctionType()->isVarArg()) calls.push_back(call);
      }
    }
    if (starts.empty() && copies.empty() && calls.empty()) return;
    Triple target(M.getTargetTriple());
    if (target.getArch()!=Triple::x86_64 || target.isOSWindows())
      report_fatal_error("native variadic storage requires the x86-64 System V ABI");
    if (InitializeNativeTarget()) report_fatal_error("cannot initialise native variadic target");
    for (auto *call:calls) callExtent(call);
    for (auto *start:starts) {
      IRBuilder<> B(start); B.CreateCall(check,{start->getArgOperand(0),B.getInt64(24)});
      registerList(start,add);
    }
    for (auto *copy:copies) {
      IRBuilder<> B(copy);
      for (Value *ap:copy->args()) B.CreateCall(check,{ap,B.getInt64(24)});
    }
  }
};
