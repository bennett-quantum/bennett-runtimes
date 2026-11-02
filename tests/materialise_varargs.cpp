// Test-fixture preparation for dialect §14.3/N23 on x86-64 System V.
// This is independent test support, not a production normaliser or a claim
// of full driver/ISA materialisation. Input source/bitcode is never edited.
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
using namespace llvm;
int main(int argc,char **argv) {
  if (argc!=3 || StringRef(argv[1])==argv[2]) {
    errs()<<"usage: materialise-varargs INPUT.bc SEPARATE-OUTPUT.bc\n"; return 1;
  }
  LLVMContext C; SMDiagnostic error;
  auto M=parseIRFile(argv[1],error,C);
  if (!M) { error.print(argv[0],errs()); return 1; }
  const DataLayout &DL=M->getDataLayout();
  if (Triple(M->getTargetTriple()).getArch()!=Triple::x86_64 ||
      !DL.isLittleEndian() || DL.getPointerSizeInBits()!=64) {
    errs()<<"this fixture uses the published x86-64 System V va-list row\n"; return 1;
  }
  auto *P=PointerType::getUnqual(C); auto *I8=Type::getInt8Ty(C);
  auto *I32=Type::getInt32Ty(C); auto *I64=Type::getInt64Ty(C);
  auto size=[&](Type *T) {
    auto S=DL.getTypeAllocSize(T);
    if (S.isScalable()) report_fatal_error("scalable fixture vararg");
    return S.getFixedValue();
  };
  auto alignment=[&](Type *T) { return std::max(uint64_t(8),DL.getABITypeAlign(T).value()); };
  DenseMap<Function *,Argument *> blocks;
  SmallVector<Function *,32> variadic;
  for (Function &F:*M) if (F.isVarArg() && !F.isIntrinsic()) variadic.push_back(&F);
  for (Function *F:variadic) {
    SmallVector<Type *,16> args(F->getFunctionType()->params()); args.push_back(P);
    auto *T=FunctionType::get(F->getReturnType(),args,false);
    auto *N=Function::Create(T,F->getLinkage(),F->getAddressSpace(),"",M.get());
    N->copyAttributesFrom(F); N->takeName(F); N->setCallingConv(F->getCallingConv());
    N->copyMetadata(F,0); N->splice(N->end(),F);
    auto next=N->arg_begin();
    for (Argument &A:F->args()) { A.replaceAllUsesWith(&*next); next->takeName(&A); ++next; }
    next->setName("nz.va"); blocks[N]=&*next;
    F->replaceAllUsesWith(N); F->eraseFromParent();
  }
  SmallVector<Instruction *,256> original;
  for (Function &F:*M) for (Instruction &I:instructions(F)) original.push_back(&I);
  for (Instruction *I:original) {
    IRBuilder<> B(I);
    if (auto *V=dyn_cast<VAArgInst>(I)) {
      auto *field=B.CreateConstGEP1_64(I8,V->getPointerOperand(),8);
      auto *old=B.CreateLoad(P,field);
      uint64_t a=alignment(V->getType());
      auto *bits=B.CreatePtrToInt(old,I64);
      auto *rounded=B.CreateAnd(B.CreateAdd(bits,B.getInt64(a-1)),B.getInt64(-a));
      auto *p=B.CreateIntToPtr(rounded,P);
      auto *value=B.CreateAlignedLoad(V->getType(),p,Align(DL.getABITypeAlign(V->getType())));
      B.CreateStore(B.CreateConstGEP1_64(I8,p,alignTo(size(V->getType()),8)),field);
      V->replaceAllUsesWith(value); V->eraseFromParent(); continue;
    }
    auto *call=dyn_cast<CallInst>(I); if (!call) continue;
    if (auto *intrinsic=dyn_cast<IntrinsicInst>(I)) {
      auto id=intrinsic->getIntrinsicID();
      if (id==Intrinsic::vastart) {
        auto found=blocks.find(call->getFunction());
        if (found==blocks.end()) report_fatal_error("va_start outside variadic definition");
        Value *ap=call->getArgOperand(0);
        B.CreateStore(B.getInt32(48),ap);
        B.CreateStore(B.getInt32(176),B.CreateConstGEP1_64(I8,ap,4));
        B.CreateStore(found->second,B.CreateConstGEP1_64(I8,ap,8));
        B.CreateStore(ConstantPointerNull::get(P),B.CreateConstGEP1_64(I8,ap,16));
        call->eraseFromParent();
      } else if (id==Intrinsic::vacopy) {
        B.CreateMemCpy(call->getArgOperand(0),MaybeAlign(1),call->getArgOperand(1),MaybeAlign(1),24);
        call->eraseFromParent();
      } else if (id==Intrinsic::vaend) call->eraseFromParent();
      continue;
    }
    auto *oldType=call->getFunctionType(); if (!oldType->isVarArg()) continue;
    unsigned named=oldType->getNumParams(); uint64_t bytes=0,maxAlign=8;
    SmallVector<uint64_t,16> offsets;
    for (unsigned i=named;i<call->arg_size();++i) {
      Type *T=call->getParamByValType(i); if (!T) T=call->getArgOperand(i)->getType();
      uint64_t a=alignment(T); maxAlign=std::max(maxAlign,a);
      bytes=alignTo(bytes,a); offsets.push_back(bytes); bytes+=alignTo(size(T),8);
    }
    IRBuilder<> entry(&*call->getFunction()->getEntryBlock().getFirstInsertionPt());
    auto *block=entry.CreateAlloca(ArrayType::get(I8,bytes),nullptr,"nz.vb"); block->setAlignment(Align(maxAlign));
    B.CreateMemSet(block,B.getInt8(0),bytes,MaybeAlign(maxAlign));
    for (unsigned i=named;i<call->arg_size();++i) {
      auto *p=B.CreateConstGEP1_64(I8,block,offsets[i-named]);
      if (Type *T=call->getParamByValType(i))
        B.CreateMemCpy(p,MaybeAlign(alignment(T)),call->getArgOperand(i),MaybeAlign(1),size(T));
      else B.CreateAlignedStore(call->getArgOperand(i),p,DL.getABITypeAlign(call->getArgOperand(i)->getType()));
    }
    SmallVector<Value *,16> actual;
    SmallVector<Type *,16> types(oldType->params()); types.push_back(P);
    SmallVector<AttributeSet,16> attrs;
    for (unsigned i=0;i<named;++i) { actual.push_back(call->getArgOperand(i)); attrs.push_back(call->getAttributes().getParamAttrs(i)); }
    actual.push_back(block); attrs.push_back(AttributeSet());
    auto *N=B.CreateCall(FunctionType::get(oldType->getReturnType(),types,false),call->getCalledOperand(),actual);
    N->setCallingConv(call->getCallingConv()); N->setDebugLoc(call->getDebugLoc());
    N->setAttributes(AttributeList::get(C,call->getAttributes().getFnAttrs(),call->getAttributes().getRetAttrs(),attrs));
    N->takeName(call); call->replaceAllUsesWith(N); call->eraseFromParent();
  }
  if (verifyModule(*M,&errs())) return 1;
  std::error_code ec; raw_fd_ostream out(argv[2],ec,sys::fs::OF_None);
  if (ec) { errs()<<ec.message()<<"\n"; return 1; }
  WriteBitcodeToFile(*M,out); return 0;
}
