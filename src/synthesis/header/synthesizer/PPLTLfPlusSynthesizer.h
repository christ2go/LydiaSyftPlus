//
// Created by Gianmarco Chris on 03/11/25
//

#ifndef LYDIASYFT_PPLTLPLUSSYNTHESIZER_H
#define LYDIASYFT_PPLTLPLUSSYNTHESIZER_H

#include <game/EmersonLei.hpp>

#include "automata/SymbolicStateDfa.h"
#include "Synthesizer.h"
#include "game/InputOutputPartition.h"
#include "lydia/parser/ppltl/driver.hpp"

// #include "automata/SymbolicStateDfa.h"
// #include "Synthesizer.h"
// #include "game/InputOutputPartition.h"
// #include "lydia/logic/ppltl/base.hpp"
// #include "lydia/parser/ppltlplus/driver.hpp"
// #include "lydia/logic/ppltlplus/base.hpp"
// #include "lydia/logic/pp_pnf.hpp"
// #include "lydia/utils/print.hpp"

namespace Syft {

    // typedef whitemech::lydia::ppltl_plus_ptr ppltl_plus_ptr;
    // typedef whitemech::lydia::ppltl_ptr ppltl_ptr;
    // typedef whitemech::lydia::PrefixQuantifier PrefixQuantifier;

    class PPLTLfPlusSynthesizer {
        private:
            /**
             * \brief Variable manager.
             */
            std::shared_ptr<VarMgr> var_mgr_;
            PPLTLPlus ppltl_plus_formula_;
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
             * \brief Construct an f.
             */
            PPLTLfPlusSynthesizer(
                PPLTLPlus ppltl_plus_formula,
                InputOutputPartition partition,
                Player starting_player,
                Player protagonist_player
            );

            /**
             * \brief Run the synthesis algorithm.
             */
            ELSynthesisResult run() const;
    };

}
#endif
