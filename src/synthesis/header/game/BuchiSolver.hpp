#pragma once

#include "automata/SymbolicStateDfa.h"
#include "game/DfaGameSynthesizer.h"
#include <memory>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <utility>
namespace Syft {

    // A standalone Büchi solver that does not inherit from DfaGameSynthesizer.
    // It needs a SymbolicStateDfa (your semi-symbolic DFA) and uses VarMgr via it.
    class BuchiSolver {
    public:
        BuchiSolver(const SymbolicStateDfa &spec,
                        Player starting_player,
                        Player protagonist_player,
                        const CUDD::BDD &state_space);

        // run the nested fixed-point Büchi solver
        SynthesisResult run();
            enum class IndepQuantMode { NO_QUANT, FORALL_INPUT, FORALL_OUTPUT };
        enum class NonStateQuantMode {
            NO_QUANT,
            EXISTS_INPUT,
            EXISTS_OUTPUT,
            FORALL_THEN_EXISTS_INPUT_OUTPUT,
            EXISTS_THEN_FORALL_OUTPUT_INPUT
        };

    private:
    // Build a BDD representing a concrete state assignment (state vars only)
    CUDD::BDD state_index_to_bdd(std::size_t index) const;

    // Print a state-set BDD by enumerating concrete state indices (safe for small automata)
    void print_state_set(const CUDD::BDD &set_bdd,
                         const std::string &label,
                         int max_enum_bits) const;

    // Print a compact summary of the current automaton (state bits, finals, transitions)
    void print_automaton_summary() const;

    // Dump the underlying DFA in a plain text format consumed by the Python loader
    // (markers: ===PYDFA_BEGIN=== ... ===PYDFA_END===)
    void DumpDFAForPython() const;

        // helpers (copied/adapted logic from your DfaGameSynthesizer snippet)
        CUDD::BDD preimage(const CUDD::BDD &winning_states) const;
                CUDD::BDD preimage_with_modes(const CUDD::BDD &winning_state, IndepQuantMode indep_mode) const;

        CUDD::BDD project_into_states(const CUDD::BDD &winning_moves) const;
                CUDD::BDD project_with_modes(const CUDD::BDD &winning_moves, NonStateQuantMode indep_mode) const;

        bool includes_initial_state(const CUDD::BDD &winning_states) const;
        CUDD::BDD computeRecurrence(const CUDD::BDD &F) const;

        CUDD::BDD normalize_state_set(const CUDD::BDD &bdd_maybe_with_io) const;

        // core algorithm pieces
        std::pair<CUDD::BDD, CUDD::BDD> computeAttr(const CUDD::BDD &region) const;
                CUDD::BDD computeCPreForPlayer(Player player, const CUDD::BDD &states) const;
            CUDD::BDD CPre_agent(const CUDD::BDD &W_states) const;
      CUDD::BDD CPre_env(const CUDD::BDD &W_states) const;


        CUDD::BDD computeCPre(const CUDD::BDD &states) const;

        // internal bookkeeping
        SymbolicStateDfa game_;
        Player starting_player_;
        Player protagonist_player_;
        std::shared_ptr<VarMgr> var_mgr_;
        CUDD::BDD state_space_;

        // precomputed helpers (like in DfaGameSynthesizer)
        std::vector<int> initial_eval_vector_;                // for Eval
        std::vector<CUDD::BDD> transition_compose_vector_;   // for VectorCompose
        CUDD::BDD input_cube_;
        CUDD::BDD output_cube_;
        std::size_t output_count_;

        // Small enum to pick quantification behaviour without other classes
        IndepQuantMode indep_quant_mode_;
        NonStateQuantMode nonstate_quant_mode_;
        // enable verbose debug printing when true
        bool debug_enabled_ = false;

        // cache last VectorCompose(W, transition_compose_vector_) result to avoid
        // recomputing the transition relation when the same W is queried repeatedly.
        // Mark mutable so const methods can update the cache.
        mutable CUDD::BDD cached_vectorcompose_W_;
        mutable CUDD::BDD cached_vectorcompose_T_;
        mutable bool has_cached_vectorcompose_ = false;
    public:
        // enable/disable debug prints at runtime
        void set_debug(bool enabled) { debug_enabled_ = enabled; }
    };

} // namespace Syft
