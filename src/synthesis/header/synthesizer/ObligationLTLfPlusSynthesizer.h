#ifndef OBLIGATION_LTLF_PLUS_SYNTHESIZER_H
#define OBLIGATION_LTLF_PLUS_SYNTHESIZER_H

#include "automata/SymbolicStateDfa.h"
#include "game/BuchiSolver.hpp"
#include "game/InputOutputPartition.h"
#include "lydia/logic/ltlfplus/base.hpp"
#include "automata/SymbolicStateDfa.h"
#include "Synthesizer.h"
#include "game/InputOutputPartition.h"
#include "lydia/parser/ppltl/driver.hpp"

#include <map>
#include <memory>
#include <string>
#include <utility>

namespace CUDD {
    // forward-declare minimal wrapper type used in signatures
    class BDD;
}

namespace Syft {

    // Forward declarations for types used by the synthesizer interface.
    // These are declared elsewhere in your codebase.
    class VarMgr;
    struct ELSynthesisResult;
    struct SynthesisResult;

    /**
     * \brief Synthesizer for LTLf+ formulas in the obligation fragment.
     *
     * This synthesizer is optimized for the obligation fragment, which consists
     * only of safety (forall) and guarantee (exists) quantifiers.
     *
     * Behaviour:
     *  - validate the formula belongs to the obligation fragment
     *  - build symbolic DFAs per color and assemble a product arena according
     *    to the boolean color formula (via build_arena_from_color_formula)
     *  - run a Büchi game solver on the arena (the arena's final_states() encodes
     *    the boolean combination of color finals)
     */
    class ObligationLTLfPlusSynthesizer {
    public:
        /**
         * Construct an ObligationLTLfPlusSynthesizer.
         *
         * \param ltlf_plus_formula  parsed LTLf+ spec (contains formula->color/quantification)
         * \param partition          input/output partition (variable names)
         * \param starting_player    who moves first each turn
         * \param protagonist_player the player we synthesise for (Agent/Environment)
         */
        ObligationLTLfPlusSynthesizer(
            LTLfPlus ltlf_plus_formula,
            InputOutputPartition partition,
            Player starting_player,
            Player protagonist_player,
            Syft::BuchiSolver::BuchiMode buechi_mode = Syft::BuchiSolver::BuchiMode::CLASSIC
        );

        /**
         * Run the synthesizer.
         *
         * \return result in ELSynthesisResult format.
         */
        ELSynthesisResult run() const;

    private:
        // --- state ---
        std::shared_ptr<VarMgr> var_mgr_;
        LTLfPlus ltlf_plus_formula_;
        Player starting_player_;
        Player protagonist_player_;
    Syft::BuchiSolver::BuchiMode buechi_mode_ = Syft::BuchiSolver::BuchiMode::CLASSIC;

        // --- core phases ---
        void validate_obligation_fragment() const;

        /**
         * \brief Builds the per-color DFAs, combines them according to the color
         * formula and returns the arena and a map of per-color finals.
         *
         * The returned map has per-color final BDDs under their color key and
         * the combined/evaluated final-states of the whole arena under key -1.
         */
        std::pair<SymbolicStateDfa, std::map<int, CUDD::BDD>>
        convert_to_symbolic_dfa() const;

        /**
         * \brief Solve the synthesis problem by running the Büchi solver on the arena.
         *
         * The arena is expected to already encode the correct accepting states
         * (i.e., arena.final_states() matches the boolean color formula).
         *
         * \param arena The product arena built from color DFAs.
         * \param color_to_final_states mapping from color -> final-states BDD (key -1 holds arena finals).
         */
        ELSynthesisResult solve_with_scc(const SymbolicStateDfa& arena,
                                         const std::map<int, CUDD::BDD>& color_to_final_states) const;

        // --- helpers exposed because they're implemented in the .cpp ---
        /**
         * Parse a boolean color formula string like "(1 & 2) | 3" and build an arena
         * DFA where numeric tokens reference DFAs provided in color_to_dfa.
         *
         * The parser clones referenced DFAs with fresh state-space for each occurrence,
         * then composes them with product_AND / product_OR according to the formula.
         */
        SymbolicStateDfa build_arena_from_color_formula(
            const std::string& color_formula,
            const std::map<int, SymbolicStateDfa>& color_to_dfa) const;

        /**
         * (Optional) Evaluate a boolean color formula by substituting color integers
         * with given BDDs and computing the resulting BDD. Useful if you prefer to
         * compute the accepting set directly rather than relying on arena.final_states().
         */
        CUDD::BDD evaluate_color_formula_with_bdds(
            const std::string& color_formula,
            const std::map<int, CUDD::BDD>& color_to_bdd) const;
    };

} // namespace Syft

#endif // OBLIGATION_LTLF_PLUS_SYNTHESIZER_H
