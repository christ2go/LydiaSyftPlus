//
// Created by Shufang Zhu on 05/03/2025.
//

#ifndef PPLTLPLUSSYNTHESIZERMP_H
#define PPLTLPLUSSYNTHESIZERMP_H


#include "automata/SymbolicStateDfa.h"
#include "Synthesizer.h"
#include "game/InputOutputPartition.h"
#include "lydia/parser/ppltl/driver.hpp"
#include "lydia/logic/ppltlplus/base.hpp"
#include "lydia/logic/pp_pnf.hpp"
#include "lydia/parser/ppltlplus/driver.hpp"
#include "lydia/utils/print.hpp"

namespace Syft {
  class PPLTLfPlusSynthesizerMP {

  private:
    std::shared_ptr<VarMgr> var_mgr_;
    Player starting_player_;
    Player protagonist_player_;
    PPLTLPlus ppltl_plus_formula_;
    std::vector<int> F_colors_;
    std::vector<int> G_colors_;

  public:
  PPLTLfPlusSynthesizerMP(
      PPLTLPlus ppltl_plus_formula,
      InputOutputPartition partition,
      Player starting_player,
      Player protagonist_player
    );


    MPSynthesisResult run() const;
  };
}


#endif //PPLTLPLUSSYNTHESIZERMP_H
