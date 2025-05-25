//
// Created by Shufang Zhu on 05/03/2025.
//

#ifndef LTLFPLUSSYNTHESIZERMP_H
#define LTLFPLUSSYNTHESIZERMP_H


#include "automata/SymbolicStateDfa.h"
#include "Synthesizer.h"
#include "game/InputOutputPartition.h"
#include "lydia/parser/ltlf/driver.hpp"
#include "lydia/logic/ltlfplus/base.hpp"
#include "lydia/logic/pnf.hpp"
#include "lydia/parser/ltlfplus/driver.hpp"
#include "lydia/utils/print.hpp"

namespace Syft {
  class LTLfPlusSynthesizerMP {

  private:
    std::shared_ptr<VarMgr> var_mgr_;
    Player starting_player_;
    Player protagonist_player_;
    LTLfPlus ltlf_plus_formula_;
    std::vector<int> F_colors_;
    std::vector<int> G_colors_;
    int game_solver_; // Manna-Pnueli-Adv=2; Manna-Pnueli=1 

  public:
    LTLfPlusSynthesizerMP(
      LTLfPlus ltlf_plus_formula,
      InputOutputPartition partition,
      Player starting_player,
      Player protagonist_player,
      int game_solver
    );


    MPSynthesisResult run() const;
  };
}


#endif //LTLFPLUSSYNTHESIZERMP_H
