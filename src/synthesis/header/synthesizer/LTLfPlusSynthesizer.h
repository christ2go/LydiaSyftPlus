//
// Created by shuzhu on 04/12/24.
//

#ifndef LYDIASYFT_LTLFPLUSSYNTHESIZER_H
#define LYDIASYFT_LTLFPLUSSYNTHESIZER_H

#include "automata/SymbolicStateDfa.h"
#include "Synthesizer.h"
#include "game/InputOutputPartition.h"
#include "lydia/parser/ltlf/driver.hpp"
#include "lydia/logic/ltlfplus/base.hpp"
#include "lydia/logic/pnf.hpp"
#include "lydia/parser/ltlfplus/driver.hpp"
#include "lydia/utils/print.hpp"

namespace Syft {

    typedef whitemech::lydia::ltlf_plus_ptr ltlf_plus_ptr;
    typedef whitemech::lydia::ltlf_ptr ltlf_ptr;
    typedef whitemech::lydia::PrefixQuantifier PrefixQuantifier;

    class LTLfPlusSynthesizer {
    private:
        /**
         * \brief Variable manager.
         */
        std::shared_ptr<VarMgr> var_mgr_;
        /**
         * \brief
         */
        std::unordered_map<ltlf_plus_ptr, std::string> formula_to_color_;
        /**
         * \brief
         */
        std::unordered_map<ltlf_plus_ptr, PrefixQuantifier> formula_to_quantification_;
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
        LTLfPlusSynthesizer(
            std::unordered_map<ltlf_plus_ptr, std::string> formula_to_color,
            std::unordered_map<ltlf_plus_ptr, PrefixQuantifier> formula_to_quantification,
            const std::string &color_formula,
            const Syft::InputOutputPartition partition, 
            Player starting_player,
            Player protagonist_player
        );

        /**
         * \brief Solves the LTLfPlus synthesis problem.
         *
         * \return The synthesis result.
         */
        ELSynthesisResult run() const;

        // /**
        //  * \brief Abstract a winning strategy for the agent.
        //  *
        //  * \return A winning strategy for the agent.
        //  */
        // std::unique_ptr<Transducer> AbstractSingleStrategy(const SynthesisResult &result) const;

    };

}


#endif //LYDIASYFT_LTLFPLUSSYNTHESIZER_H
