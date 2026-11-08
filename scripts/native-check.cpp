// Comparison-only adapter, applied AFTER materialisation. This is not a
// normaliser and never writes its input or enters device identity/ISA mapping.
#include "llvm/ADT/SmallVector.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
using namespace llvm;

int main(int argc, char **argv) {
  if (argc!=3) { errs()<<"usage: native-check-tool MATERIALISED.bc CHECKED.bc\n"; return 1; }
  LLVMContext C; SMDiagnostic error;
  auto M=parseIRFile(argv[1],error,C);
  if (!M) { error.print(argv[0],errs()); return 1; }
  const DataLayout &DL=M->getDataLayout();
  if (!DL.isLittleEndian() || DL.getPointerSizeInBits()!=64) {
    errs()<<"native check requires the published little-endian LP64 layout\n"; return 1;
  }
  if (M->getNamedGlobal("__bennett_rt_native_objects")) {
    errs()<<"native check already applied\n"; return 1;
  }
  auto *V=Type::getVoidTy(C);
  auto *I64=Type::getInt64Ty(C);
  auto *P=PointerType::getUnqual(C);
  auto check=M->getOrInsertFunction("__bennett_rt_native_check",V,P,I64);
  auto fault=M->getOrInsertFunction("__bennett_nz_fault",V,I64);
  auto enter=M->getOrInsertFunction("__bennett_rt_native_stack_enter",P);
  auto add=M->getOrInsertFunction("__bennett_rt_native_stack_add",V,P,I64);
  auto leave=M->getOrInsertFunction("__bennett_rt_native_stack_leave",V,P);
  auto vaPush=M->getOrInsertFunction("__bennett_rt_native_va_push",V,I64);
  auto vaPop=M->getOrInsertFunction("__bennett_rt_native_va_pop",V);
  auto vaOverflow=M->getOrInsertFunction("__bennett_rt_native_va_overflow_size",I64);
  const uint64_t regSaveBytes=6*8+8*16; // x86-64 SysV register save area
  // One table entry per global, never a coalesced section envelope: crossing
  // adjacent objects is invalid even when both objects are live.
  auto *object=StructType::get(P,I64);
  SmallVector<Constant *,128> objects;
  for (GlobalVariable &G:M->globals()) {
    if (G.getName().starts_with("llvm.") || G.isThreadLocal() ||
        !G.getValueType()->isSized() || (G.isDeclaration() && G.use_empty())) continue;
    auto size=DL.getTypeAllocSize(G.getValueType());
    if (size.isScalable()) { errs()<<"unmaterialised scalable global\n"; return 1; }
    objects.push_back(ConstantStruct::get(object,ConstantExpr::getPointerCast(&G,P),
                                         ConstantInt::get(I64,size.getFixedValue())));
  }
  auto *array=ArrayType::get(object,objects.size());
  auto *table=new GlobalVariable(*M,array,true,GlobalValue::ExternalLinkage,
                                 ConstantArray::get(array,objects),"__bennett_rt_native_objects");
  table->setAlignment(Align(8));
  auto *count=new GlobalVariable(*M,I64,true,GlobalValue::ExternalLinkage,
                                 ConstantInt::get(I64,objects.size()),"__bennett_rt_native_object_count");
  count->setAlignment(Align(8));
  auto sizeOf=[&](Type *T) -> uint64_t {
    auto S=DL.getTypeStoreSize(T);
    if (S.isScalable()) report_fatal_error("unmaterialised scalable access");
    return S.getFixedValue();
  };
  auto allocationSize=[&](Type *T) -> uint64_t {
    if (!T || !T->isSized()) report_fatal_error("unsized native object");
    auto S=DL.getTypeAllocSize(T);
    if (S.isScalable()) report_fatal_error("unmaterialised scalable object");
    return S.getFixedValue();
  };
  auto range=[&](Instruction *I, Value *address,Value *size) {
    IRBuilder<> B(I);
    B.CreateCall(check,{B.CreatePointerCast(address,P),B.CreateZExtOrTrunc(size,I64)});
  };
  auto guard=[&](Instruction *I,Value *condition,unsigned code) {
    auto *term=SplitBlockAndInsertIfThen(condition,I,true);
    IRBuilder<> B(term); B.CreateCall(fault,{B.getInt64(code)});
  };
  // x86-64 System V variadic storage is created by the backend, never by an
  // IR alloca: a variadic callee spills rdi..r9 and xmm0..7 into a fixed
  // 176-byte register save area (6*8 + 8*16), and remaining arguments occupy
  // a caller-built overflow area whose extent only the caller knows. Walk
  // the call operands in ABI order over the six GP and eight SSE registers
  // to recover that exact extent; anything this adapter cannot classify
  // exactly fails loud instead of registering an envelope.
  auto stackArgBytes=[&](CallBase *CB) -> uint64_t {
    unsigned gp=0, sse=0; uint64_t stack=0;
    auto memory=[&](uint64_t size,uint64_t align) {
      if (align>16) report_fatal_error("unmaterialised over-aligned variadic stack argument");
      stack=alignTo(stack,std::max<uint64_t>(align,8))+alignTo(size,8);
    };
    for (unsigned a=0;a<CB->arg_size();++a) {
      if (Type *T=CB->getParamByValType(a)) {
        uint64_t align=CB->getParamAlign(a).value_or(DL.getABITypeAlign(T)).value();
        memory(allocationSize(T),align); continue;
      }
      Type *T=CB->getArgOperand(a)->getType();
      if (T->isIntegerTy() || T->isPointerTy()) {
        unsigned bits=T->isPointerTy()?64:T->getIntegerBitWidth();
        if (bits<=64) { if (gp<6) ++gp; else memory(8,8); }
        else if (bits<=128) { if (gp+2<=6) gp+=2; else memory(16,16); }
        else report_fatal_error("unmaterialised wide integer variadic argument");
      } else if (T->isHalfTy() || T->isFloatTy() || T->isDoubleTy() ||
                 (T->isVectorTy() && !cast<VectorType>(T)->getElementCount().isScalable() &&
                  DL.getTypeSizeInBits(T).getFixedValue()<=128)) {
        if (sse<8) ++sse; else memory(alignTo(sizeOf(T),8),8);
      } else if (T->isX86_FP80Ty() || T->isFP128Ty()) memory(16,16);
      else report_fatal_error("unmaterialised variadic argument class");
    }
    return stack;
  };
  for (Function &F:*M) {
    if (F.isDeclaration()) continue;
    SmallVector<Instruction *,256> original;
    bool hasStackObject=false;
    for (Instruction &I:instructions(F)) {
      original.push_back(&I); hasStackObject|=isa<AllocaInst>(I);
      // va_start registers backend variadic storage in this frame, so the
      // frame needs the enter/leave bracket even without an IR alloca.
      if (auto *intrinsic=dyn_cast<IntrinsicInst>(&I))
        hasStackObject|=intrinsic->getIntrinsicID()==Intrinsic::vastart;
    }
    for (Argument &A:F.args()) hasStackObject|=A.hasByValAttr();
    F.removeFnAttr(Attribute::Memory);
    F.removeFnAttr(Attribute::Speculatable);
    F.removeFnAttr(Attribute::WillReturn);
    F.removeFnAttr(Attribute::AllocKind);
    F.removeFnAttr(Attribute::AllocSize);
    F.removeFnAttr(Attribute::NoFree);
    F.addFnAttr("no-builtins");
    Value *mark=nullptr;
    if (hasStackObject) {
      IRBuilder<> B(&*F.getEntryBlock().getFirstInsertionPt());
      mark=B.CreateCall(enter,{},"native.frame");
      // byval creates a distinct ABI-owned copy even when there is no
      // explicit alloca. Include DataLayout tail padding, but no envelope
      // covering other arguments or the native stack. The existing mark
      // removes each copy on every materialised return from this callee.
      for (Argument &A:F.args())
        if (A.hasByValAttr())
          B.CreateCall(add,{B.CreatePointerCast(&A,P),
                            B.getInt64(allocationSize(A.getParamByValType()))});
    }
    for (Instruction *I:original) {
      IRBuilder<> B(I);
      // The caller's implicit ABI read happens before the callee can check
      // its copy. Validate the complete source, including padding, before
      // native codegen emits that read (null/short/dead sources fault 7).
      if (auto *CB=dyn_cast<CallBase>(I))
        for (unsigned a=0;a<CB->arg_size();++a)
          if (Type *T=CB->getParamByValType(a))
            range(I,CB->getArgOperand(a),B.getInt64(allocationSize(T)));
      if (auto *CB=dyn_cast<CallBase>(I); CB && CB->getFunctionType()->isVarArg() &&
          !(CB->getCalledFunction() && CB->getCalledFunction()->isIntrinsic())) {
        if (!isa<CallInst>(I)) report_fatal_error("unmaterialised variadic invoke");
        // Publish the exact overflow extent for the callee's va_start; the
        // pop also covers callees that never call va_start.
        B.CreateCall(vaPush,{B.getInt64(stackArgBytes(CB))});
        IRBuilder<> after(I->getNextNode());
        after.CreateCall(vaPop,{});
      }
      if (auto *intrinsic=dyn_cast<IntrinsicInst>(I);
          intrinsic && intrinsic->getIntrinsicID()==Intrinsic::vastart) {
        // Register the two backend areas the va_list now names (System V
        // layout: overflow_arg_area at byte 8, reg_save_area at byte 16).
        // These inserted loads are not in the original snapshot, so they
        // are not themselves checked. A null area registers zero bytes.
        IRBuilder<> after(I->getNextNode());
        Value *ap=intrinsic->getArgOperand(0);
        auto *I8=Type::getInt8Ty(C);
        auto *overflow=after.CreateLoad(P,after.CreateConstGEP1_64(I8,ap,8));
        overflow->setAlignment(Align(1));
        auto *regsave=after.CreateLoad(P,after.CreateConstGEP1_64(I8,ap,16));
        regsave->setAlignment(Align(1));
        auto *regSize=after.CreateSelect(after.CreateIsNull(regsave),
                                         after.getInt64(0),after.getInt64(regSaveBytes));
        after.CreateCall(add,{after.CreatePointerCast(regsave,P),regSize});
        auto *extent=after.CreateCall(vaOverflow,{});
        auto *ovSize=after.CreateSelect(after.CreateIsNull(overflow),
                                        after.getInt64(0),extent);
        after.CreateCall(add,{after.CreatePointerCast(overflow,P),ovSize});
      }
      if (auto *L=dyn_cast<LoadInst>(I)) range(I,L->getPointerOperand(),B.getInt64(sizeOf(L->getType())));
      else if (auto *S=dyn_cast<StoreInst>(I)) range(I,S->getPointerOperand(),B.getInt64(sizeOf(S->getValueOperand()->getType())));
      else if (auto *A=dyn_cast<AtomicRMWInst>(I)) range(I,A->getPointerOperand(),B.getInt64(sizeOf(A->getValOperand()->getType())));
      else if (auto *A=dyn_cast<AtomicCmpXchgInst>(I)) range(I,A->getPointerOperand(),B.getInt64(sizeOf(A->getCompareOperand()->getType())));
      else if (auto *A=dyn_cast<AllocaInst>(I)) {
        IRBuilder<> after(A->getNextNode());
        auto *n=after.CreateMul(after.CreateZExtOrTrunc(A->getArraySize(),I64),
                               after.getInt64(DL.getTypeAllocSize(A->getAllocatedType()).getFixedValue()));
        after.CreateCall(add,{after.CreatePointerCast(A,P),n});
      } else if (isa<ReturnInst>(I) && mark) B.CreateCall(leave,{mark});
      else if (auto *MI=dyn_cast<MemIntrinsic>(I)) {
        range(I,MI->getRawDest(),MI->getLength());
        if (auto *MT=dyn_cast<MemTransferInst>(MI)) range(I,MT->getRawSource(),MI->getLength());
      } else if (auto *BO=dyn_cast<BinaryOperator>(I)) {
        unsigned op=BO->getOpcode();
        if (op==Instruction::UDiv || op==Instruction::URem || op==Instruction::SDiv || op==Instruction::SRem) {
          auto *T=dyn_cast<IntegerType>(BO->getType());
          if (!T) report_fatal_error("unmaterialised vector division");
          auto *zero=B.CreateICmpEQ(BO->getOperand(1),ConstantInt::get(T,0));
          guard(I,zero,5);
          if (op==Instruction::SDiv || op==Instruction::SRem) {
            IRBuilder<> D(I);
            auto *min=D.CreateICmpEQ(BO->getOperand(0),ConstantInt::get(C,APInt::getSignedMinValue(T->getBitWidth())));
            auto *minusOne=D.CreateICmpEQ(BO->getOperand(1),ConstantInt::getSigned(T,-1));
            guard(I,D.CreateAnd(min,minusOne),6);
          }
        }
      } else if (isa<UnreachableInst>(I)) {
        auto *previous=dyn_cast_or_null<CallInst>(I->getPrevNode());
        if (!previous || previous->getCalledFunction()!=fault.getCallee()) B.CreateCall(fault,{B.getInt64(4)});
      } else if (auto *call=dyn_cast<CallInst>(I)) {
        call->removeFnAttr(Attribute::Memory);
        call->removeFnAttr(Attribute::Speculatable);
        call->removeFnAttr(Attribute::WillReturn);
        call->removeFnAttr(Attribute::AllocKind);
        call->removeFnAttr(Attribute::AllocSize);
        call->removeFnAttr(Attribute::NoFree);
        call->setTailCallKind(CallInst::TCK_None);
        auto *callee=call->getCalledFunction();
        // LLVM's libcall simplifier otherwise replaces even a defined strlen
        // or removes unused malloc calls, discarding materialised faults and
        // allocation history. Those are ordinary functions at this boundary.
        if (!callee || !callee->isIntrinsic()) call->addFnAttr(Attribute::NoBuiltin);
        if (!callee) continue; // indirect callees' bodies have the same checks
        auto name=callee->getName();
        if (name=="llvm.trap" || name=="llvm.debugtrap") {
          B.CreateCall(fault,{B.getInt64(4)}); call->eraseFromParent();
        } else if (name=="memcpy" || name=="memmove" || name=="__bennett_rt_memmove") {
          range(I,call->getArgOperand(0),call->getArgOperand(2));
          range(I,call->getArgOperand(1),call->getArgOperand(2));
        } else if (name=="memset" || name=="__bennett_rt_memset")
          range(I,call->getArgOperand(0),call->getArgOperand(2));
      }
    }
  }
  if (verifyModule(*M,&errs())) return 1;
  std::error_code ec; raw_fd_ostream output(argv[2],ec,sys::fs::OF_None);
  if (ec) { errs()<<ec.message()<<"\n"; return 1; }
  WriteBitcodeToFile(*M,output);
  return 0;
}
