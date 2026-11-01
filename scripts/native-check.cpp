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
  auto range=[&](Instruction *I, Value *address,Value *size) {
    IRBuilder<> B(I);
    B.CreateCall(check,{B.CreatePointerCast(address,P),B.CreateZExtOrTrunc(size,I64)});
  };
  auto guard=[&](Instruction *I,Value *condition,unsigned code) {
    auto *term=SplitBlockAndInsertIfThen(condition,I,true);
    IRBuilder<> B(term); B.CreateCall(fault,{B.getInt64(code)});
  };
  for (Function &F:*M) {
    if (F.isDeclaration()) continue;
    SmallVector<Instruction *,256> original;
    bool hasAlloca=false;
    for (Instruction &I:instructions(F)) {
      original.push_back(&I); hasAlloca|=isa<AllocaInst>(I);
    }
    F.removeFnAttr(Attribute::Memory);
    F.removeFnAttr(Attribute::Speculatable);
    F.removeFnAttr(Attribute::WillReturn);
    F.removeFnAttr(Attribute::AllocKind);
    F.removeFnAttr(Attribute::AllocSize);
    F.removeFnAttr(Attribute::NoFree);
    F.addFnAttr("no-builtins");
    Value *mark=nullptr;
    if (hasAlloca) {
      IRBuilder<> B(&*F.getEntryBlock().getFirstInsertionPt());
      mark=B.CreateCall(enter,{},"native.frame");
    }
    for (Instruction *I:original) {
      IRBuilder<> B(I);
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
