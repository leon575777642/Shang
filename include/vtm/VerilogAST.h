//===------------- VLang.h - Verilog HDL writing engine ---------*- C++ -*-===//
//
//                            The Verilog Backend
//
// Copyright: 2010 by Hongbin Zheng. all rights reserved.
// IMPORTANT: This software is supplied to you by Hongbin Zheng in consideration
// of your agreement to the following terms, and your use, installation, 
// modification or redistribution of this software constitutes acceptance
// of these terms.  If you do not agree with these terms, please do not use, 
// install, modify or redistribute this software. You may not redistribute, 
// install copy or modify this software without written permission from 
// Hongbin Zheng. 
//
//===----------------------------------------------------------------------===//
//
// The VLang provide funtions to complete common Verilog HDL writing task.
//
//===----------------------------------------------------------------------===//
#ifndef VBE_VLANG_H
#define VBE_VLANG_H

#include "llvm/Pass.h"
#include "llvm/Function.h"
#include "llvm/Target/Mangler.h"
#include "llvm/Target/TargetData.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Format.h"

namespace llvm {
class MachineBasicBlock;

// Leaf node type of Verilog AST.
enum VASTTypes {
  vastPort,
  vastSignal,
  vastParameter,
  vastFirstDeclType = vastPort,
  vastLastDeclType = vastParameter,

  vastModule
};

class VASTNode {
  std::string Name;
  std::string Comment;
  const unsigned short T;
  unsigned short SubclassData;
protected:
  VASTNode(VASTTypes NodeT, const std::string &name, unsigned short subclassData,
           const std::string &comment)
    : Name(name), Comment(comment), T(NodeT), SubclassData(subclassData) {}

  unsigned short getSubClassData() const { return SubclassData; }

public:
  virtual ~VASTNode() {}

  unsigned getASTType() const { return T; }

  const std::string &getName() const { return Name; } 
  
  virtual void print(raw_ostream &OS) const = 0;
};

class VASTValue : public VASTNode {
  bool IsReg;
  unsigned InitVal;
protected:
  VASTValue(VASTTypes DeclType, const std::string &Name, unsigned BitWidth, bool isReg,
           unsigned initVal, const std::string &Comment)
    : VASTNode(DeclType, Name, BitWidth, Comment), IsReg(isReg), InitVal(initVal)
  {
    assert(DeclType >= vastFirstDeclType && DeclType <= vastLastDeclType
           && "Bad DeclType!");
  }
public:
  unsigned short getBitWidth() const { return getSubClassData(); }
  bool isRegister() const { return IsReg; }


  void printReset(raw_ostream &OS) const;
};

class VASTPort : public VASTValue {
  bool IsInput;
public:
  VASTPort(const std::string &Name, unsigned BitWidth, bool isInput, bool isReg,
           const std::string &Comment)
    : VASTValue(vastPort, Name, BitWidth, isReg, 0, Comment), IsInput(isInput) {
    assert(!(isInput && isRegister()) && "Bad port decl!");
  }
  
  bool isInput() const { return IsInput; }

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  static inline bool classof(const VASTPort *A) { return true; }
  static inline bool classof(const VASTNode *A) {
    return A->getASTType() == vastPort;
  }

  void print(raw_ostream &OS) const;
  void printExternalDriver(raw_ostream &OS, uint64_t InitVal = 0) const;
};

class VASTSignal : public VASTValue {
public:
  VASTSignal(const std::string &Name, unsigned BitWidth, bool isReg,
             const std::string &Comment)
    : VASTValue(vastSignal, Name, BitWidth, isReg, 0, Comment) {}

  void print(raw_ostream &OS) const;
  void printDecl(raw_ostream &OS) const;
};

// The class that represent Verilog modulo.
class VASTModule : public VASTNode {
public:
  typedef SmallVector<VASTPort*, 16> PortVector;
  typedef SmallVector<VASTSignal*, 128> SignalVector;
private:
  // Dirty Hack:
  // Buffers
  raw_string_ostream StateDecl, DataPath, ControlBlock, SeqCompute;
  PortVector Ports;
  SignalVector Signals;
public:
  enum PortTypes {
    Clk = 0,
    RST,
    Start,
    SpecialInPortEnd,
    Finish = SpecialInPortEnd,
    SpecialOutPortEnd,
    NumSpecialPort = SpecialOutPortEnd,
    Others
  };

  VASTModule(const std::string &Name) : VASTNode(vastModule, Name, 0, ""),
    StateDecl(*(new std::string())),
    DataPath(*(new std::string())),
    ControlBlock(*(new std::string())),
    SeqCompute(*(new std::string())) {
    Ports.append(NumSpecialPort, 0);
  }

  ~VASTModule();
  void clear();

  // Allow user to add ports.
  VASTPort *addInputPort(const std::string &Name, unsigned BitWidth,
                         PortTypes T = Others,
                         const std::string &Comment = "") {
    VASTPort *Port = new VASTPort(Name, BitWidth, true, false, Comment);
    if (T < SpecialInPortEnd) {
      assert(Ports[T] == 0 && "Special port exist!");
      Ports[T] = Port;
      return Port;
    }

    assert(T == Others && "Wrong port type!");
    Ports.push_back(Port);
    return Port;
  }

  VASTPort *addOutputPort(const std::string &Name, unsigned BitWidth,
                          PortTypes T = Others,
                          const std::string &Comment = "") {
    VASTPort *Port = new VASTPort(Name, BitWidth, false, true, Comment);
    if (SpecialInPortEnd <= T && T < SpecialOutPortEnd) {
      assert(Ports[T] == 0 && "Special port exist!");
      Ports[T] = Port;
      return Port;
    }

    assert(T == Others && "Wrong port type!");
    Ports.push_back(Port);
    return Port;
  }

  void printModuleDecl(raw_ostream &OS) const;

  const PortVector &getPorts() const { return Ports; }
  const VASTPort &getInputPort(unsigned i) const {
    // FIXME: Check if out of range.
    return *Ports[i];
  }

  const VASTPort &getCommonInPort(unsigned i) const {
    // FIXME: Check if out of range.
    return *Ports[i + VASTModule::SpecialOutPortEnd];
  }

  typedef PortVector::iterator port_iterator;
  typedef PortVector::const_iterator const_port_iterator;

  port_iterator ports_begin() { return Ports.begin(); }
  const_port_iterator ports_begin() const { return Ports.begin(); }

  port_iterator ports_end() { return Ports.end(); }
  const_port_iterator ports_end() const { return Ports.end(); }

  VASTSignal *addRegister(const std::string &Name, unsigned BitWidth,
                          const std::string &Comment = "") {
    VASTSignal *Reg = new VASTSignal(Name, BitWidth, true, Comment);
    Signals.push_back(Reg);
    return Reg;
  }

  VASTSignal *addWire(const std::string &Name, unsigned BitWidth,
                      const std::string &Comment = "") {
    VASTSignal *Signal = new VASTSignal(Name, BitWidth, false, Comment);
    Signals.push_back(Signal);
    return Signal;
  }

  void printSignalDecl(raw_ostream &OS, unsigned indent = 2);
  void printRegisterReset(raw_ostream &OS, unsigned indent = 6);

  void print(raw_ostream &OS) const;

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  static inline bool classof(const VASTModule *A) { return true; }
  static inline bool classof(const VASTNode *A) {
    return A->getASTType() == vastModule;
  }


  raw_ostream &getStateDeclBuffer(unsigned ind = 2) {
    return StateDecl.indent(ind);
  }

  std::string &getStateDeclStr() {
    return StateDecl.str();
  }

  raw_ostream &getSeqComputeBuffer(unsigned ind = 0) {
    return SeqCompute.indent(ind);
  }

  std::string &getSeqComputeStr() {
    return SeqCompute.str();
  }

  raw_ostream &getControlBlockBuffer(unsigned ind = 0) {
    return ControlBlock.indent(ind);
  }

  std::string &getControlBlockStr() {
    return ControlBlock.str();
  }

  raw_ostream &getDataPathBuffer(unsigned ind = 0) {
    return DataPath.indent(ind);
  }

  std::string &getDataPathStr() {
    return DataPath.str();
  }
};

std::string verilogConstToStr(Constant *C);

std::string verilogConstToStr(uint64_t value,unsigned bitwidth,
                              bool isMinValue);

std::string verilogBitRange(unsigned UB, unsigned LB = 0, bool printOneBit = true);

raw_ostream &verilogCommentBegin(raw_ostream &ss);

raw_ostream &verilogAlwaysBegin(raw_ostream &ss, unsigned ind,
                                const std::string &Clk = "clk",
                                const std::string &ClkEdge = "posedge",
                                const std::string &Rst = "rstN",
                                const std::string &RstEdge = "negedge");

raw_ostream &verilogParam(raw_ostream &ss, const std::string &Name,
                          unsigned BitWidth, unsigned Val);

raw_ostream &verilogAlwaysEnd(raw_ostream &ss, unsigned ind);

raw_ostream &verilogSwitchCase(raw_ostream &ss, const std::string &StateName);

raw_ostream &verilogMatchCase(raw_ostream &ss, const std::string &StateName);

raw_ostream &verilogIfBegin(raw_ostream &ss, const std::string &Condition);

raw_ostream &verilogIfElse(raw_ostream &ss, bool Begin = true);

raw_ostream &verilogBegin(raw_ostream &ss);

raw_ostream &verilogEnd(raw_ostream &ss);

raw_ostream &verilogEndSwitch(raw_ostream &ss);

raw_ostream &verilogEndModule(raw_ostream &ss);


} // end namespace

#endif
