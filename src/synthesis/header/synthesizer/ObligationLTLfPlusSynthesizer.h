#ifndef OBLIGATION_LTLF_PLUS_SYNTHESIZER_H
#define OBLIGATION_LTLF_PLUS_SYNTHESIZER_H

#include "automata/SymbolicStateDfa.h"
#include "Synthesizer.h"
#include "game/InputOutputPartition.h"

namespace Syft {

    /**
     * \brief Synthesizer for LTLf+ formulas in the obligation fragment.
     * 
     * This synthesizer is optimized for the obligation fragment, which consists
     * only of safety (forall) and guarantee (exists) quantifiers, excluding
     * recurrence (forall-exists) and persistence (exists-forall).
     * 
     * The synthesizer:
     * 1. Validates that the formula is in the obligation fragment (throws error if not)
     * 2. Converts the formula to symbolic state DFA (reusing existing conversion logic)
     * 3. Uses an SCC-based solver for efficient synthesis
     */
    class ObligationLTLfPlusSynthesizer {
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
         * \brief Validates that the formula is in the obligation fragment.
         * 
         * \throws std::runtime_error if the formula is not in the obligation fragment.
         */
        void validate_obligation_fragment() const;

        /**
         * \brief Converts the LTLf+ formula to symbolic state DFA.
         * 
         * Reuses the conversion logic from LTLfPlusSynthesizer but only
         * handles Forall and Exists quantifiers (obligation fragment).
         * 
         * \return A pair containing the arena (product of all DFAs) and
         *         a map from color to final states BDD.
         */
        std::pair<SymbolicStateDfa, std::map<int, CUDD::BDD>> convert_to_symbolic_dfa() const;

        /**
         * \brief Solves the synthesis problem using SCC-based approach.
         * 
         * This is the new solver that computes strongly connected components
         * and performs the synthesis algorithm.
         * 
         * \param arena The symbolic state DFA representing the game arena.
         * \param color_to_final_states Map from color to final states BDD.
         * \return The synthesis result.
         */
        ELSynthesisResult solve_with_scc(const SymbolicStateDfa& arena,
                                         const std::map<int, CUDD::BDD>& color_to_final_states) const;

        /**
         * \brief Build arena DFA by parsing the color formula and combining DFAs.
         * 
         * Parses the boolean formula (e.g., "(1 & 2) | 3") and builds the
         * product DFA using AND for conjunction and OR for disjunction.
         * 
         * \param color_formula The boolean formula string.
         * \param color_to_dfa Map from color to individual DFAs.
         * \return The combined arena DFA.
         */
        SymbolicStateDfa build_arena_from_color_formula(
            const std::string& color_formula,
            const std::map<int, SymbolicStateDfa>& color_to_dfa) const;

    public:
        /**
         * \brief Construct an ObligationLTLfPlusSynthesizer.
         */
        ObligationLTLfPlusSynthesizer(
            LTLfPlus ltlf_plus_formula,
            InputOutputPartition partition,
            Player starting_player,
            Player protagonist_player
        );

        /**
         * \brief Solves the LTLfPlus synthesis problem for obligation fragment.
         *
         * \return The synthesis result.
         */
        ELSynthesisResult run() const;
    };

}

#endif // OBLIGATION_LTLF_PLUS_SYNTHESIZER_H

