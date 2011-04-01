//====---- VTMFrameInfo.h - Define TargetFrameInfo for VTM --*- C++ -*----====//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//
//
//===----------------------------------------------------------------------===//

#ifndef VTM_FRAMEINFO_H
#define VTM_FRAMEINFO_H
#include "VSubtarget.h"
#include "vtm/VTM.h"
#include "llvm/Target/TargetFrameLowering.h"

namespace llvm {
class VSubtarget;

class VFrameInfo : public TargetFrameLowering {
protected:
  const VSubtarget &STI;

public:
  explicit VFrameInfo(const VSubtarget &sti)
    : TargetFrameLowering(TargetFrameLowering::StackGrowsDown, 2, -2), STI(sti) {
  }

  /// emitProlog/emitEpilog - These methods insert prolog and epilog code into
  /// the function.
  void emitPrologue(MachineFunction &MF) const;
  void emitEpilogue(MachineFunction &MF, MachineBasicBlock &MBB) const;

  bool hasFP(const MachineFunction &MF) const { return false; }
};

} // End llvm namespace

#endif