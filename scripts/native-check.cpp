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

static uint64_t sizeOf(const DataLayout &DL,Type *T) {
  auto S=DL.getTypeStoreSize(T);
  if (S.isScalable()) report_fatal_error("unmaterialised scalable access");
  return S.getFixedValue();
}
static uint64_t allocationSize(const DataLayout &DL,Type *T) {
  if (!T || !T->isSized()) report_fatal_error("unsized native object");
  auto S=DL.getTypeAllocSize(T);
  if (S.isScalable()) report_fatal_error("unmaterialised scalable object");
  return S.getFixedValue();
}

// The overflow pointer from va_start skips named stack operands. Walk the
// complete argument list to account for register use and alignment, then
// publish only stack end minus named stack end. No caller frame envelope.
//
// System V AMD64 ABI 1.0, 3.2.3 Classification / Passing; N-bit integers
// and bfloat: 3.1.2. LLVM operands here are frontend ABI-lowered forms.
// https://gitlab.com/x86-psABIs/x86-64-ABI/-/raw/master/x86-64-ABI/low-level-sys-info.tex
// LLVM operand                    ABI / target storage        Stack fallback
// integer <=64, pointer           INTEGER: 1 GP               8, align 8
// integer 65..127                 INTEGER: 2 GP chunks         16, align 8
// integer exactly 128 (__int128)  INTEGER: 2 GP                16, align 16
// half, bfloat, float, double     SSE: 1 XMM                   round size to 8
// fp128                          SSE,SSEUP: 1 XMM             16, align 16
// x86_fp80                       X87,X87UP: MEMORY            16, align 16
// <1 x i1/i8/i16/i32>             legalized INTEGER: 1 GP      8, align 8
// <1 x i64>                      legalized MMX: MEMORY        8, align 8
// <1 x half/bfloat>              legalized SSE: 1 XMM         16, align 16
// <1 x float/double/fp128/fp80>   legalized scalar: as above   as above
// power-of-two vectors <=128     SSE/SSEUP: 1 XMM             16, align 16
//   with i1/i8/i16/i32/i64/half/bfloat/float/double elements;
//   with at most 16 i1 lanes; small vectors widen to 128 bits before passing.
// named vectors 256/512 bits     backend MEMORY (*)           32/64, align 32/64
// unnamed vectors 256/512 bits   frontend byval: MEMORY       allocation, align 32/64
// byval aggregate/vector         MEMORY (already lowered)     allocation size,
//                                                              explicit/ABI align
// (*) The source ABI specifies SSE/SSEUP for named AVX operands, which
// arrive as valid direct IR vectors. Both LLVM backends nevertheless pass
// these on the stack at variadic calls (X86CallingConv.td CC_X86_64_C), so
// named accounting follows caller storage and consumes no SSE register.
// Other vectors and scalars lacking an exact target mapping publish an unclassified marker;
// diagnose only if the callee executes va_start, never at checked linking.
// SSEUP shares the preceding SSE register. An operand that cannot fit its
// entire register bank falls back to stack without consuming registers.
// Stack slots round size/alignment up to at least eight bytes, preserving
// the operand's stack alignment; named stack operands are never published.
enum class ArgumentStorage { GP, SSE, Memory, StaticChain, Unclassified };
struct ArgumentLayout {
  ArgumentStorage storage;
  unsigned registers;
  uint64_t bytes,alignment;
};
// System V AMD64 ABI 3.2.3 register banks and minimum stack slot.
static constexpr unsigned GPRegisters=6, SSERegisters=8;
static constexpr uint64_t StackSlot=8;
// Unknown layout is diagnosed at va_start, never registered as an extent.
static constexpr uint64_t UnclassifiedExtent=UINT64_MAX;

static ArgumentLayout classifyVector(FixedVectorType *T,bool named,const DataLayout &DL) {
  Type *element=T->getElementType();
  unsigned count=T->getNumElements();
  uint64_t bits=DL.getTypeSizeInBits(T).getFixedValue();
  if (count==1) {
    if (element->isIntegerTy(64)) return {ArgumentStorage::Memory,0,8,8};
    if (element->isIntegerTy() && element->getIntegerBitWidth()<=32)
      return {ArgumentStorage::GP,1,8,8};
    if (element->isX86_FP80Ty()) return {ArgumentStorage::Memory,0,16,16};
    if (element->isHalfTy() || element->isBFloatTy())
      return {ArgumentStorage::SSE,1,16,16}; // legalized to a full XMM vector
    if (element->isFloatTy() || element->isDoubleTy() || element->isFP128Ty())
      return {ArgumentStorage::SSE,1,alignTo(sizeOf(DL,element),StackSlot),DL.getABITypeAlign(element).value()};
    return {ArgumentStorage::Unclassified,0,0,1};
  }
  // Other representations may scalarize/split during target legalization.
  // Do not infer their register count from total bit width alone.
  bool integerElement=element->isIntegerTy(1) || element->isIntegerTy(8) ||
                      element->isIntegerTy(16) || element->isIntegerTy(32) || element->isIntegerTy(64);
  bool sseElement=integerElement || element->isHalfTy() || element->isBFloatTy() ||
                  element->isFloatTy() || element->isDoubleTy();
  // These backends stack-pass direct named wide vectors in vararg calls.
  // This differs from the source ABI's named SSE/SSEUP rule; the operand is
  // already lowered, so account for the actual caller storage.
  if (named && isPowerOf2_32(count) && sseElement && !element->isIntegerTy(1) &&
      (bits==256 || bits==512))
    return {ArgumentStorage::Memory,0,bits/8,bits/8};
  if (!isPowerOf2_32(count) || bits>128 || !sseElement || (element->isIntegerTy(1) && count>16))
    return {ArgumentStorage::Unclassified,0,0,1};
  // Small multi-lane vectors widen to 128 bits before ABI assignment.
  return {ArgumentStorage::SSE,1,16,16};
}

static ArgumentLayout classifyArgument(CallBase *CB,unsigned index,const DataLayout &DL) {
  if (CB->paramHasAttr(index,Attribute::Nest))
    return {ArgumentStorage::StaticChain,0,0,1}; // r10, outside the GP sequence
  if (Type *T=CB->getParamByValType(index))
    return {ArgumentStorage::Memory,0,allocationSize(DL,T),
            CB->getParamAlign(index).value_or(DL.getABITypeAlign(T)).value()};
  Type *T=CB->getArgOperand(index)->getType();
  switch (T->getTypeID()) {
  case Type::PointerTyID:
    return {ArgumentStorage::GP,1,8,8};
  case Type::IntegerTyID: {
    unsigned bits=T->getIntegerBitWidth();
    if (bits<=64) return {ArgumentStorage::GP,1,8,8};
    if (bits<=128) return {ArgumentStorage::GP,2,16,bits==128?16U:8U};
    return {ArgumentStorage::Unclassified,0,0,1};
  }
  case Type::FixedVectorTyID:
    return classifyVector(cast<FixedVectorType>(T),index<CB->getFunctionType()->getNumParams(),DL);
  case Type::HalfTyID: case Type::BFloatTyID:
  case Type::FloatTyID: case Type::DoubleTyID: case Type::FP128TyID:
    return {ArgumentStorage::SSE,1,alignTo(sizeOf(DL,T),StackSlot),DL.getABITypeAlign(T).value()};
  case Type::X86_FP80TyID:
    return {ArgumentStorage::Memory,0,16,16};
  default:
    return {ArgumentStorage::Unclassified,0,0,1};
  }
}

struct ArgumentArea {
  unsigned gp=0,sse=0;
  uint64_t stack=0;
};
static void placeArgument(ArgumentArea &area,const ArgumentLayout &argument) {
  switch (argument.storage) {
  case ArgumentStorage::StaticChain: return;
  case ArgumentStorage::Unclassified: llvm_unreachable("unclassified argument reached layout");
  case ArgumentStorage::GP:
    if (area.gp+argument.registers<=GPRegisters) { area.gp+=argument.registers; return; }
    break;
  case ArgumentStorage::SSE:
    if (area.sse+argument.registers<=SSERegisters) { area.sse+=argument.registers; return; }
    break;
  case ArgumentStorage::Memory: break;
  }
  area.stack=alignTo(area.stack,std::max(argument.alignment,StackSlot))+alignTo(argument.bytes,StackSlot);
}

static uint64_t variadicOverflowBytes(CallBase *CB,const DataLayout &DL) {
  unsigned named=CB->getFunctionType()->getNumParams();
  if (CB->arg_size()<named)
    report_fatal_error("variadic call has fewer operands than named parameters");
  ArgumentArea area;
  uint64_t namedEnd=0;
  for (unsigned index=0;index<CB->arg_size();++index) {
    auto argument=classifyArgument(CB,index,DL);
    if (argument.storage==ArgumentStorage::Unclassified) return UnclassifiedExtent;
    placeArgument(area,argument);
    if (index+1==named) namedEnd=area.stack;
  }
  return area.stack-namedEnd;
}

struct NativeChecks {
  Module &M;
  FunctionCallee check,fault,enter,add,leave,vaPush,vaPop,vaOverflow;
};
static NativeChecks declareChecks(Module &M) {
  auto &C=M.getContext();
  auto *V=Type::getVoidTy(C);
  auto *I64=Type::getInt64Ty(C);
  auto *P=PointerType::getUnqual(C);
  auto check=M.getOrInsertFunction("__bennett_rt_native_check",V,P,I64);
  auto fault=M.getOrInsertFunction("__bennett_nz_fault",V,I64);
  auto enter=M.getOrInsertFunction("__bennett_rt_native_stack_enter",P);
  auto add=M.getOrInsertFunction("__bennett_rt_native_stack_add",V,P,I64);
  auto leave=M.getOrInsertFunction("__bennett_rt_native_stack_leave",V,P);
  auto vaPush=M.getOrInsertFunction("__bennett_rt_native_va_push",V,I64);
  auto vaPop=M.getOrInsertFunction("__bennett_rt_native_va_pop",V);
  auto vaOverflow=M.getOrInsertFunction("__bennett_rt_native_va_overflow_size",I64);
  return {M,check,fault,enter,add,leave,vaPush,vaPop,vaOverflow};
}
static void range(NativeChecks &checks,Instruction *I,Value *address,Value *size) {
  IRBuilder<> B(I);
  auto *P=PointerType::getUnqual(checks.M.getContext());
  auto *I64=B.getInt64Ty();
  B.CreateCall(checks.check,{B.CreatePointerCast(address,P),B.CreateZExtOrTrunc(size,I64)});
}
static void guard(NativeChecks &checks,Instruction *I,Value *condition,unsigned code) {
  auto *term=SplitBlockAndInsertIfThen(condition,I,true);
  IRBuilder<> B(term); B.CreateCall(checks.fault,{B.getInt64(code)});
}

static bool publishGlobals(Module &M) {
  const DataLayout &DL=M.getDataLayout();
  auto &C=M.getContext();
  auto *P=PointerType::getUnqual(C);
  auto *I64=Type::getInt64Ty(C);
  // One table entry per global, never a coalesced section envelope: crossing
  // adjacent objects is invalid even when both objects are live.
  auto *object=StructType::get(P,I64);
  SmallVector<Constant *,128> objects;
  for (GlobalVariable &G:M.globals()) {
    if (G.getName().starts_with("llvm.") || G.isThreadLocal() ||
        !G.getValueType()->isSized() || (G.isDeclaration() && G.use_empty())) continue;
    auto size=DL.getTypeAllocSize(G.getValueType());
    if (size.isScalable()) { errs()<<"unmaterialised scalable global\n"; return false; }
    objects.push_back(ConstantStruct::get(object,ConstantExpr::getPointerCast(&G,P),
                                         ConstantInt::get(I64,size.getFixedValue())));
  }
  auto *array=ArrayType::get(object,objects.size());
  auto *table=new GlobalVariable(M,array,true,GlobalValue::ExternalLinkage,
                                 ConstantArray::get(array,objects),"__bennett_rt_native_objects");
  table->setAlignment(Align(8));
  // These definitions are never preemptible in the static checked link.
  // Without dso_local, PIE-level codegen loads their addresses through a
  // GOT whose orphan sections shift device-visible data by O-level.
  table->setDSOLocal(true);
  auto *count=new GlobalVariable(M,I64,true,GlobalValue::ExternalLinkage,
                                 ConstantInt::get(I64,objects.size()),"__bennett_rt_native_object_count");
  count->setAlignment(Align(8));
  count->setDSOLocal(true);
  return true;
}

static void diagnoseVariadicLayout(NativeChecks &checks,Value *extent,Instruction *next) {
  IRBuilder<> B(next);
  auto *unknown=B.CreateICmpEQ(extent,B.getInt64(UnclassifiedExtent));
  auto *term=SplitBlockAndInsertIfThen(unknown,next,true);
  IRBuilder<> diagnostic(term);
  auto *message=diagnostic.CreateGlobalString("unclassified SysV variadic argument storage");
  auto infrastructure=checks.M.getOrInsertFunction("__bennett_rt_native_infrastructure",
      diagnostic.getVoidTy(),diagnostic.getPtrTy());
  diagnostic.CreateCall(infrastructure,{message});
}

static void instrumentVarargs(NativeChecks &checks,Instruction *I) {
  auto &C=checks.M.getContext();
  const auto &DL=checks.M.getDataLayout();
  auto *P=PointerType::getUnqual(C);
  const uint64_t regSaveBytes=6*8+8*16; // x86-64 SysV register save area
  IRBuilder<> B(I);
  if (auto *CB=dyn_cast<CallBase>(I); CB && CB->getFunctionType()->isVarArg() &&
      !(CB->getCalledFunction() && CB->getCalledFunction()->isIntrinsic())) {
    if (!isa<CallInst>(I)) report_fatal_error("unmaterialised variadic invoke");
    // Publish the exact overflow extent for the callee's va_start; the
    // pop also covers callees that never call va_start.
    B.CreateCall(checks.vaPush,{B.getInt64(variadicOverflowBytes(CB,DL))});
    IRBuilder<> after(I->getNextNode());
    after.CreateCall(checks.vaPop,{});
  }
  if (auto *intrinsic=dyn_cast<IntrinsicInst>(I);
      intrinsic && intrinsic->getIntrinsicID()==Intrinsic::vastart) {
    // Register the two backend areas the va_list now names (System V
    // layout: overflow_arg_area at byte 8, reg_save_area at byte 16).
    // These inserted loads are not in the original snapshot, so they
    // are not themselves checked. A null area registers zero bytes.
    Instruction *next=I->getNextNode();
    IRBuilder<> after(next);
    Value *ap=intrinsic->getArgOperand(0);
    auto *I8=Type::getInt8Ty(C);
    auto *overflow=after.CreateLoad(P,after.CreateConstGEP1_64(I8,ap,8));
    overflow->setAlignment(Align(1));
    auto *regsave=after.CreateLoad(P,after.CreateConstGEP1_64(I8,ap,16));
    regsave->setAlignment(Align(1));
    auto *regSize=after.CreateSelect(after.CreateIsNull(regsave),
                                     after.getInt64(0),after.getInt64(regSaveBytes));
    after.CreateCall(checks.add,{after.CreatePointerCast(regsave,P),regSize});
    auto *extent=after.CreateCall(checks.vaOverflow,{});
    diagnoseVariadicLayout(checks,extent,next);
    after.SetInsertPoint(next);
    auto *ovSize=after.CreateSelect(after.CreateIsNull(overflow),
                                    after.getInt64(0),extent);
    after.CreateCall(checks.add,{after.CreatePointerCast(overflow,P),ovSize});
  }
}

static void instrumentInstruction(NativeChecks &checks,Instruction *I,Value *mark) {
  auto &C=checks.M.getContext();
  const auto &DL=checks.M.getDataLayout();
  auto *P=PointerType::getUnqual(C);
  auto *I64=Type::getInt64Ty(C);
  IRBuilder<> B(I);
  // The caller's implicit ABI read happens before the callee can check
  // its copy. Validate the complete source, including padding, before
  // native codegen emits that read (null/short/dead sources fault 7).
  if (auto *CB=dyn_cast<CallBase>(I))
    for (unsigned a=0;a<CB->arg_size();++a)
      if (Type *T=CB->getParamByValType(a))
        range(checks,I,CB->getArgOperand(a),B.getInt64(allocationSize(DL,T)));
  instrumentVarargs(checks,I);
  if (auto *L=dyn_cast<LoadInst>(I)) range(checks,I,L->getPointerOperand(),B.getInt64(sizeOf(DL,L->getType())));
  else if (auto *S=dyn_cast<StoreInst>(I)) range(checks,I,S->getPointerOperand(),B.getInt64(sizeOf(DL,S->getValueOperand()->getType())));
  else if (auto *A=dyn_cast<AtomicRMWInst>(I)) range(checks,I,A->getPointerOperand(),B.getInt64(sizeOf(DL,A->getValOperand()->getType())));
  else if (auto *A=dyn_cast<AtomicCmpXchgInst>(I)) range(checks,I,A->getPointerOperand(),B.getInt64(sizeOf(DL,A->getCompareOperand()->getType())));
  else if (auto *A=dyn_cast<AllocaInst>(I)) {
    IRBuilder<> after(A->getNextNode());
    auto *n=after.CreateMul(after.CreateZExtOrTrunc(A->getArraySize(),I64),
                           after.getInt64(DL.getTypeAllocSize(A->getAllocatedType()).getFixedValue()));
    after.CreateCall(checks.add,{after.CreatePointerCast(A,P),n});
  } else if (isa<ReturnInst>(I) && mark) B.CreateCall(checks.leave,{mark});
  else if (auto *MI=dyn_cast<MemIntrinsic>(I)) {
    range(checks,I,MI->getRawDest(),MI->getLength());
    if (auto *MT=dyn_cast<MemTransferInst>(MI)) range(checks,I,MT->getRawSource(),MI->getLength());
  } else if (auto *BO=dyn_cast<BinaryOperator>(I)) {
    unsigned op=BO->getOpcode();
    if (op==Instruction::UDiv || op==Instruction::URem || op==Instruction::SDiv || op==Instruction::SRem) {
      auto *T=dyn_cast<IntegerType>(BO->getType());
      if (!T) report_fatal_error("unmaterialised vector division");
      auto *zero=B.CreateICmpEQ(BO->getOperand(1),ConstantInt::get(T,0));
      guard(checks,I,zero,5);
      if (op==Instruction::SDiv || op==Instruction::SRem) {
        IRBuilder<> D(I);
        auto *min=D.CreateICmpEQ(BO->getOperand(0),ConstantInt::get(C,APInt::getSignedMinValue(T->getBitWidth())));
        auto *minusOne=D.CreateICmpEQ(BO->getOperand(1),ConstantInt::getSigned(T,-1));
        guard(checks,I,D.CreateAnd(min,minusOne),6);
      }
    }
  } else if (isa<UnreachableInst>(I)) {
    auto *previous=dyn_cast_or_null<CallInst>(I->getPrevNode());
    if (!previous || previous->getCalledFunction()!=checks.fault.getCallee()) B.CreateCall(checks.fault,{B.getInt64(4)});
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
    if (!callee) return; // indirect callees' bodies have the same checks
    auto name=callee->getName();
    if (name=="llvm.trap" || name=="llvm.debugtrap") {
      B.CreateCall(checks.fault,{B.getInt64(4)}); call->eraseFromParent();
    } else if (name=="memcpy" || name=="memmove" || name=="__bennett_rt_memmove") {
      range(checks,I,call->getArgOperand(0),call->getArgOperand(2));
      range(checks,I,call->getArgOperand(1),call->getArgOperand(2));
    } else if (name=="memset" || name=="__bennett_rt_memset")
      range(checks,I,call->getArgOperand(0),call->getArgOperand(2));
  }
}

static void instrumentFunction(NativeChecks &checks,Function &F) {
  auto &C=checks.M.getContext();
  const auto &DL=checks.M.getDataLayout();
  auto *P=PointerType::getUnqual(C);
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
    mark=B.CreateCall(checks.enter,{},"native.frame");
    // byval creates a distinct ABI-owned copy even when there is no
    // explicit alloca. Include DataLayout tail padding, but no envelope
    // covering other arguments or the native stack. The existing mark
    // removes each copy on every materialised return from this callee.
    for (Argument &A:F.args())
      if (A.hasByValAttr())
        B.CreateCall(checks.add,{B.CreatePointerCast(&A,P),
                          B.getInt64(allocationSize(DL,A.getParamByValType()))});
  }
  for (Instruction *I:original) instrumentInstruction(checks,I,mark);
}

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
  auto checks=declareChecks(*M);
  if (!publishGlobals(*M)) return 1;
  for (Function &F:*M) if (!F.isDeclaration()) instrumentFunction(checks,F);
  if (verifyModule(*M,&errs())) return 1;
  std::error_code ec; raw_fd_ostream output(argv[2],ec,sys::fs::OF_None);
  if (ec) { errs()<<ec.message()<<"\n"; return 1; }
  WriteBitcodeToFile(*M,output);
  return 0;
}
