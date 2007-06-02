//===--- CGStmt.cpp - Emit LLVM Code from Statements ----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Chris Lattner and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This contains code to emit Stmt nodes as LLVM code.
//
//===----------------------------------------------------------------------===//

#include "CodeGenFunction.h"
#include "clang/AST/AST.h"
#include "llvm/Constants.h"
#include "llvm/DerivedTypes.h"
#include "llvm/Function.h"
using namespace llvm;
using namespace clang;
using namespace CodeGen;

//===----------------------------------------------------------------------===//
//                              Statement Emission
//===----------------------------------------------------------------------===//

void CodeGenFunction::EmitStmt(const Stmt *S) {
  assert(S && "Null statement?");
  
  switch (S->getStmtClass()) {
  default:
    // Must be an expression in a stmt context.  Emit the value and ignore the
    // result.
    if (const Expr *E = dyn_cast<Expr>(S)) {
      EmitExpr(E);
    } else {
      printf("Unimplemented stmt!\n");
      S->dump();
    }
    break;
  case Stmt::NullStmtClass: break;
  case Stmt::CompoundStmtClass: EmitCompoundStmt(cast<CompoundStmt>(*S)); break;
  case Stmt::LabelStmtClass:    EmitLabelStmt(cast<LabelStmt>(*S));       break;
  case Stmt::GotoStmtClass:     EmitGotoStmt(cast<GotoStmt>(*S));         break;
  case Stmt::IfStmtClass:       EmitIfStmt(cast<IfStmt>(*S));             break;
  case Stmt::ReturnStmtClass:   EmitReturnStmt(cast<ReturnStmt>(*S));     break;
  case Stmt::DeclStmtClass:     EmitDeclStmt(cast<DeclStmt>(*S));         break;
  }
}

void CodeGenFunction::EmitCompoundStmt(const CompoundStmt &S) {
  // FIXME: handle vla's etc.
  
  for (CompoundStmt::const_body_iterator I = S.body_begin(), E = S.body_end();
       I != E; ++I)
    EmitStmt(*I);
}

void CodeGenFunction::EmitBlock(BasicBlock *BB) {
  // Emit a branch from this block to the next one if this was a real block.  If
  // this was just a fall-through block after a terminator, don't emit it.
  BasicBlock *LastBB = Builder.GetInsertBlock();
  
  if (LastBB->getTerminator()) {
    // If the previous block is already terminated, don't touch it.
  } else if (LastBB->empty() && LastBB->getValueName() == 0) {
    // If the last block was an empty placeholder, remove it now.
    // TODO: cache and reuse these.
    Builder.GetInsertBlock()->eraseFromParent();
  } else {
    // Otherwise, create a fall-through branch.
    Builder.CreateBr(BB);
  }
  CurFn->getBasicBlockList().push_back(BB);
  Builder.SetInsertPoint(BB);
}

void CodeGenFunction::EmitLabelStmt(const LabelStmt &S) {
  llvm::BasicBlock *NextBB = getBasicBlockForLabel(&S);
  
  EmitBlock(NextBB);
  EmitStmt(S.getSubStmt());
}

void CodeGenFunction::EmitGotoStmt(const GotoStmt &S) {
  Builder.CreateBr(getBasicBlockForLabel(S.getLabel()));
  
  // Emit a block after the branch so that dead code after a goto has some place
  // to go.
  Builder.SetInsertPoint(new BasicBlock("", CurFn));
}

void CodeGenFunction::EmitIfStmt(const IfStmt &S) {
  // Emit the if condition.
  ExprResult CondVal = EmitExpr(S.getCond());
  QualType CondTy = S.getCond()->getType().getCanonicalType();
  
  // C99 6.8.4.1: The first substatement is executed if the expression compares
  // unequal to 0.  The condition must be a scalar type.
  llvm::Value *BoolCondVal =
    EvaluateScalarValueToBool(CondVal, S.getCond()->getType());
  
  BasicBlock *ContBlock = new BasicBlock("ifend");
  BasicBlock *ThenBlock = new BasicBlock("ifthen");
  BasicBlock *ElseBlock = ContBlock;
  
  if (S.getElse())
    ElseBlock = new BasicBlock("ifelse");
  
  // Insert the conditional branch.
  Builder.CreateCondBr(BoolCondVal, ThenBlock, ElseBlock);
  
  // Emit the 'then' code.
  EmitBlock(ThenBlock);
  EmitStmt(S.getThen());
  Builder.CreateBr(ContBlock);
  
  // Emit the 'else' code if present.
  if (const Stmt *Else = S.getElse()) {
    EmitBlock(ElseBlock);
    EmitStmt(Else);
    Builder.CreateBr(ContBlock);
  }
  
  // Emit the continuation block for code after the if.
  EmitBlock(ContBlock);
}

/// EmitReturnStmt - Note that due to GCC extensions, this can have an operand
/// if the function returns void, or may be missing one if the function returns
/// non-void.  Fun stuff :).
void CodeGenFunction::EmitReturnStmt(const ReturnStmt &S) {
  ExprResult RetVal;
  
  // Emit the result value, even if unused, to evalute the side effects.
  const Expr *RV = S.getRetValue();
  if (RV)
    RetVal = EmitExpr(RV);
  
  if (CurFuncDecl->getType()->isVoidType()) {
    // If the function returns void, emit ret void, and ignore the retval.
    Builder.CreateRetVoid();
  } else if (RV == 0) {
    // "return;" in a function that returns a value.
    const llvm::Type *RetTy = CurFn->getFunctionType()->getReturnType();
    if (RetTy == llvm::Type::VoidTy)
      Builder.CreateRetVoid();   // struct return etc.
    else
      Builder.CreateRet(llvm::UndefValue::get(RetTy));
  } else if (RetVal.isScalar()) {
    // FIXME: return should coerce its operand to the return type!
    Builder.CreateRet(RetVal.getVal());
  } else {
    assert(0 && "FIXME: aggregate return unimp");
  }
  
  // Emit a block after the branch so that dead code after a goto has some place
  // to go.
  Builder.SetInsertPoint(new BasicBlock("", CurFn));
}

