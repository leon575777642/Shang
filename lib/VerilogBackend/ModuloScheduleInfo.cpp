//===- ModuloScheduleInfo.cpp - ModuleSchedule information analyze -*- C++ -*-===//
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
// This file implement the ModuleSchedule information computation pass describe
// in
// Josep, L. (1996). Swing Modulo Scheduling: A Lifetime-Sensitive Approach.
//
//===----------------------------------------------------------------------===//

#include "ModuloScheduleInfo.h"
#include "HWAtomPasses.h"

#include "llvm/Analysis/LoopInfo.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/Support/CommandLine.h"
#define DEBUG_TYPE "vbe-ms-info"
#include "llvm/Support/Debug.h"

#include <algorithm>

using namespace llvm;
using namespace esyn;


static cl::opt<bool>
NoModuloSchedule("disable-modulo-schedule",
          cl::desc("vbe - Do not preform modulo schedule"),
          cl::Hidden, cl::init(false));

//===----------------------------------------------------------------------===//
typedef GraphTraits<esyn::HWAtom*> HWAtomSccGT;
typedef GraphTraits<const esyn::HWAtom*> ConstHWAtomSccGT;

typedef scc_iterator<HWAtom*, HWAtomSccGT> dep_scc_iterator;
typedef scc_iterator<const HWAtom*, ConstHWAtomSccGT> const_dep_scc_iterator;

//===----------------------------------------------------------------------===//
char ModuloScheduleInfo::ID = 0;

RegisterPass<ModuloScheduleInfo> X("vbe-ms-info",
                                  "vbe - Compute necessary information for modulo"
                                  " schedule scheduling passes");

void ModuloScheduleInfo::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequiredTransitive<HWAtomInfo>();
  AU.addRequiredTransitive<ResourceConfig>();
  AU.addRequired<LoopInfo>();
  AU.setPreservesAll();
}

bool ModuloScheduleInfo::runOnFunction(Function &F) {
  HI = &getAnalysis<HWAtomInfo>();
  LI = &getAnalysis<LoopInfo>();
  return false;
}

bool ModuloScheduleInfo::isModuloSchedulable(FSMState &State) const {
  // Are we disable modulo schedule?
  if (NoModuloSchedule) return false;
  
  BasicBlock *BB = State.getBasicBlock();
  Loop *L = LI->getLoopFor(BB);
  // States that not in loops are not MSable.
  if (!L) return false;

  // Only schedule single block loops.
  if (L->getBlocks().size() > 1)
    return false;
  
  return true;
}

unsigned ModuloScheduleInfo::computeRecII(scc_vector &Scc) {
  assert(Scc.size() > 1 && "No self loop expect in DDG!");
  unsigned totalLatency = 0;
  unsigned totalItDist = 0;
  
  std::sort(Scc.begin(), Scc.end(), HWAtom::top_sort());
  //
  DEBUG(
  for (scc_vector::const_iterator I = Scc.begin(), E = Scc.end();
      I != E; ++I)
    (*I)->dump();
  );

  typedef SmallVector<std::pair<const HWAtom*, HWEdge*>, 4> BEVector;
  BEVector BackEdges;
  const HWAtom *FirstAtom = Scc.front();
  std::map<const HWAtom*, unsigned> MaxLatency, MaxItDist;
  // All nodes longest path.
  for (scc_vector::const_iterator I = Scc.begin(), E = Scc.end();
      I != E; ++I) {
    const HWAtom *CurAtom = *I;
    for (HWAtom::const_use_iterator UI = CurAtom->use_begin(),
        UE = CurAtom->use_end();UI != UE; ++UI) {
      // FIXME: Take care of back edge!
      const HWAtom *UseAtom = *UI;
      HWEdge *Edge = UseAtom->getEdgeFrom(CurAtom);
      // Ignore the backedge at this moment.
      if (Edge->isBackEdge()) {
        BackEdges.push_back(std::make_pair(UseAtom, Edge));
        continue;
      }
      
      MaxLatency[UseAtom] = std::max(MaxLatency[CurAtom] + CurAtom->getLatency(),
                                     MaxLatency[UseAtom]);
      MaxItDist[UseAtom] = std::max(MaxItDist[CurAtom] + Edge->getItDst(),
                                    MaxItDist[UseAtom]);
    }
  }
  totalLatency = 0;
  totalItDist = 0;
  DEBUG(dbgs() << "\n\n\nBackedge:\n");
  // For each backedge.
  for (BEVector::iterator I = BackEdges.begin(), E = BackEdges.end();
      I != E; ++I) {
    HWEdge *BE = I->second;
    const HWAtom *Src = BE->getSrc(), *Dst = I->first; 
    DEBUG(Src->dump());
    DEBUG(dbgs() << "->\n");
    DEBUG(Dst->dump());
    //assert(MaxLatency[Dst] == 0 && MaxItDist[Dst] == 0
    //       && "Back edge dst reachable from other Nodes?");
    totalLatency = std::max(totalLatency,
                            MaxLatency[Src] + Src->getLatency()
                            - MaxLatency[Dst]); // Dirty hack.
    totalItDist = std::max(totalItDist,
                           MaxItDist[Src] + BE->getItDst()
                           - MaxItDist[Dst]);
    DEBUG(dbgs() << "latancy: " << totalLatency
                 << " dist: " << totalItDist << "\n\n\n");
  }

  assert(totalItDist != 0 && "No cross iteration dependence?");
  return ceil((double)totalLatency / totalItDist);
}


unsigned ModuloScheduleInfo::computeRecMII(FSMState &State) {
  HWAtom *Root = &State;
  unsigned MaxRecII = 1;
  for (dep_scc_iterator SCCI = dep_scc_iterator::begin(Root),
      SCCE = dep_scc_iterator::end(Root); SCCI != SCCE; ++SCCI) {
    scc_vector &Atoms = *SCCI;
    if (Atoms.size() == 1) {
      assert(!SCCI.hasLoop() && "No self loop expect in DDG!");
      TrivialNodes.push_back(Atoms[0]);
      continue;
    }

    DEBUG(dbgs() << "SCC found:\n");
    unsigned RecII = computeRecII(Atoms);
    // Update maxrecii.
    MaxRecII = std::max(RecII, MaxRecII);
    RecList.insert(std::make_pair(RecII, Atoms));
    DEBUG(dbgs() << "RecII: " << RecII << '\n');
  }

  DEBUG(dbgs() << "RecMII: " << MaxRecII << '\n');
  return MaxRecII;
}

unsigned ModuloScheduleInfo::computeResMII(FSMState &State) const {
  std::map<HWFUnit*, unsigned> TotalResUsage;
  for (FSMState::iterator I = State.begin(), E = State.end(); I != E; ++I)
    if (HWAOpFU *A = dyn_cast<HWAOpFU>(*I)) {
      ++TotalResUsage[A->getFUnit()];
    }

  unsigned MaxResII = 0;
  typedef std::map<HWFUnit*, unsigned>::iterator UsageIt;
  for (UsageIt I = TotalResUsage.begin(), E = TotalResUsage.end(); I != E; ++I){
      MaxResII = std::max(MaxResII,
                          I->second / I->first->getTotalFUs());
  }
  DEBUG(dbgs() << "ResMII: " << MaxResII << '\n');
  return MaxResII;
}

ModuloScheduleInfo::~ModuloScheduleInfo() {
  clear();
}

void ModuloScheduleInfo::clear() {
  RecList.clear();
  TrivialNodes.clear();
}
