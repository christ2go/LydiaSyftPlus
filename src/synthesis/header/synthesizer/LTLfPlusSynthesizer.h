//
// Created by shuzhu on 04/12/24.
//

#ifndef LYDIASYFT_LTLFPLUSSYNTHESIZER_H
#define LYDIASYFT_LTLFPLUSSYNTHESIZER_H

#include <game/EmersonLei.hpp>

#include "automata/SymbolicStateDfa.h"
#include "Synthesizer.h"
#include "game/InputOutputPartition.h"
#include "lydia/parser/ltlf/driver.hpp"


namespace Syft {

    class LTLfPlusSynthesizer {
    private:
        /**
         * \brief Variable manager.
         */
        std::shared_ptr<VarMgr> var_mgr_;
        LTLfPlus ltlf_plus_formula_;
        /**
         * \brief The player that moves first each turn.
         */
        Player starting_player_;
        /**
         * \brief The player for which we aim to find the winning strategy.
         */
        Player protagonist_player_;
        /**
         * \brief The color formula representing the Zielonka tree
         */
        std::string color_formula_;
        mutable std::shared_ptr<EmersonLei> emerson_lei_;

    public:

        /**
         * \brief Construct an LtlfPlusSynthesizer.
         */
        LTLfPlusSynthesizer(
            LTLfPlus ltlf_plus_formula,
            InputOutputPartition partition,
            Player starting_player,
            Player protagonist_player
        );

        /**
         * \brief Solves the LTLfPlus synthesis problem.
         *
         * \return The synthesis result.
         */
        ELSynthesisResult run() const;

        // EmersonLei::OneStepSynReturn synthesize(std::string X, ELSynthesisResult result) const;


    };

}


#endif //LYDIASYFT_LTLFPLUSSYNTHESIZER_H