//
// Created by Gianmarco Chris on 03/11/25
//

#ifndef LYDIASYFT_PPLTLPLUSSYNTHESIZER_H
#define LYDIASYFT_PPLTLPLUSSYNTHESIZER_H

#include "automata/SymbolicStateDfa.h"
#include "Synthesizer.h"
#include "game/InputOutputPartition.h"
#include "lydia/logic/ppltl/base.hpp"
#include "lydia/parser/ppltlplus/driver.hpp"
#include "lydia/logic/ppltlplus/base.hpp"
#include "lydia/logic/pp_pnf.hpp"
#include "lydia/utils/print.hpp"

namespace Syft {

    typedef whitemech::lydia::ppltl_plus_ptr ppltl_plus_ptr;
    typedef whitemech::lydia::ppltl_ptr ppltl_ptr;
    typedef whitemech::lydia::PrefixQuantifier PrefixQuantifier;

    class PPLTLPlusSynthesizer {
        private:
            /**
             * \brief Variable manager.
             */
            std::shared_ptr<VarMgr> var_mgr_;
            /**
             * \brief
             */
            std::unordered_map<ppltl_plus_ptr, std::string> formula_to_color_;
            /**
             * \brief
             */
            std::unordered_map<ppltl_plus_ptr, PrefixQuantifier> formula_to_quantification_;
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
             * \brief Construct an PPLTLPlusSynthesizer.
             */
            PPLTLPlusSynthesizer(
                std::unordered_map<ppltl_plus_ptr, std::string> formula_to_color,
                std::unordered_map<ppltl_plus_ptr, PrefixQuantifier> formula_to_quantification,
                const std::string &color_formula,
                const Syft::InputOutputPartition partition, 
                Player starting_player,
                Player protagonist_player
            );

            /**
             * \brief Run the synthesis algorithm.
             */
            SynthesisResult run() const;
    };

}
#endif
