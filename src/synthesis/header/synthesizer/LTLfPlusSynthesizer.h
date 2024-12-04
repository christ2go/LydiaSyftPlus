//
// Created by shuzhu on 04/12/24.
//

#ifndef LYDIASYFT_LTLFPLUSSYNTHESIZER_H
#define LYDIASYFT_LTLFPLUSSYNTHESIZER_H

#include "automata/SymbolicStateDfa.h"
#include "Synthesizer.h"
#include "game/InputOutputPartition.h"

namespace Syft {

    class LTLfPlusSynthesizer {
    private:
        /**
         * \brief Variable manager.
         */
        std::shared_ptr<VarMgr> var_mgr_;
        /**
         * \brief
         */
        std::map<char, Syft::LTLfPlus> ltlfplus_spec_;
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

    public:

        /**
         * \brief Construct an LtlfPlusSynthesizer.
         */
        LTLfPlusSynthesizer(std::map<char, Syft::LTLfPlus> &ltlfplus_spec,
                            const std::string &color_formula,
                            const Syft::InputOutputPartition partition, Player starting_player,
                            Player protagonist_player);

        /**
         * \brief Solves the LTLfPlus synthesis problem.
         *
         * \return The synthesis result.
         */
        SynthesisResult run() const;

        /**
         * \brief Abstract a winning strategy for the agent.
         *
         * \return A winning strategy for the agent.
         */
        std::unique_ptr<Transducer> AbstractSingleStrategy(const SynthesisResult &result) const;

    };

}


#endif //LYDIASYFT_LTLFPLUSSYNTHESIZER_H
