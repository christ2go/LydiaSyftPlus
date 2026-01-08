#include "game/BuchiSolver.hpp"
#include "automata/SymbolicStateDfa.h"
#include <iostream>
#include <tuple>

namespace Syft {
        using Indep = BuchiSolver::IndepQuantMode;
        using NonState = BuchiSolver::NonStateQuantMode;

    // compute quant modes for (starting_player, protagonist) as in DfaGameSynthesizer
    static std::pair<BuchiSolver::IndepQuantMode, BuchiSolver::NonStateQuantMode>
    compute_modes_for(Player starting_player, Player protagonist_player) {

        if (starting_player == Player::Environment) {
            if (protagonist_player == Player::Environment) {
                return {Indep::NO_QUANT, NonState::FORALL_THEN_EXISTS_INPUT_OUTPUT};
            } else {
                return {Indep::NO_QUANT, NonState::FORALL_THEN_EXISTS_INPUT_OUTPUT};
            }
        } else {
            if (protagonist_player == Player::Environment) {
                return {Indep::NO_QUANT, NonState::EXISTS_THEN_FORALL_OUTPUT_INPUT};
            } else {
                return {Indep::NO_QUANT, NonState::EXISTS_THEN_FORALL_OUTPUT_INPUT};
            }
        }
    }



    // Helper: build the BDD that corresponds to a concrete state index (pure state BDD).
// Uses the automaton's state variables (NOT next-state or IO vars).
CUDD::BDD BuchiSolver::state_index_to_bdd(std::size_t index) const {
    auto mgr = var_mgr_->cudd_mgr();
    auto automaton_id = game_.automaton_id();
    auto state_vars = var_mgr_->get_state_variables(automaton_id);
    std::size_t bit_count = state_vars.size();

    CUDD::BDD b = mgr->bddOne();
    for (std::size_t i = 0; i < bit_count; ++i) {
        bool bit = ((index >> i) & 1ull);
        if (bit) b *= state_vars[i];
        else b *= !state_vars[i];
    }
    return b;
}
// Pretty-print a pure-state BDD by enumerating concrete state indices present in it.
// Safety: only enumerates if bit_count <= max_enum_bits (default 20).
void BuchiSolver::print_state_set(const CUDD::BDD &set_bdd, const std::string &label, int max_enum_bits = 20) const {
    auto automaton_id = game_.automaton_id();
    auto state_vars = var_mgr_->get_state_variables(automaton_id);
    std::size_t bit_count = state_vars.size();

    if (!debug_enabled_) return;

    std::cout << "[BuchiSolver PRINT] " << label << " nodes=" << set_bdd.nodeCount()
              << " isZero=" << set_bdd.IsZero() << " isOne=" << set_bdd.IsOne() << "\n";

    if (bit_count > (std::size_t)max_enum_bits) {
        std::cout << "[BuchiSolver PRINT] skipping enumeration (bit_count=" << bit_count
                  << " > " << max_enum_bits << ")\n";
        return;
    }

    uint64_t total = 1ull << bit_count;
    std::vector<std::size_t> members;
    members.reserve(std::min<uint64_t>(total, 256));
    for (uint64_t s = 0; s < total; ++s) {
        CUDD::BDD state_bdd = state_index_to_bdd(s);
        // membership test: check if (state_bdd & set_bdd) is non-zero
        if (!( (state_bdd & set_bdd).IsZero() )) {
            members.push_back(static_cast<std::size_t>(s));
            if (members.size() >= 256) break;
        }
    }

    std::cout << "[BuchiSolver PRINT] " << label << " members (count=" << members.size() << "): ";
    for (auto m : members) std::cout << m << " ";
    if (members.size() == 256) std::cout << "...";
    std::cout << "\n";
}

// Print a compact summary of the automaton: number of state bits and the raw BDDs
void BuchiSolver::print_automaton_summary() const {
    auto automaton_id = game_.automaton_id();
    auto state_vars = var_mgr_->get_state_variables(automaton_id);
    std::size_t bit_count = state_vars.size();
    if (!debug_enabled_) return;

    std::cout << "[BuchiSolver PRINT] Automaton id=" << automaton_id
              << " state_bits=" << bit_count
              << " transition_funcs=" << game_.transition_function().size()
              << " final_states_nodes=" << game_.final_states().nodeCount()
              << "\n";

    // print final states BDD
    std::cout << "[BuchiSolver PRINT] final_states BDD = " << game_.final_states() << "\n";

    // print each transition function (bit-level) up to a reasonable number
    auto tfs = game_.transition_function();
    for (std::size_t i = 0; i < tfs.size(); ++i) {
        std::cout << "[BuchiSolver PRINT] transition bit " << i << " nodes=" << tfs[i].nodeCount()
                  << " bdd=" << tfs[i] << "\n";
    }
}


    BuchiSolver::BuchiSolver(
        const SymbolicStateDfa& spec,
        Player starting_player,
        Player protagonist_player,
        const CUDD::BDD &state_space
    ) : game_(spec),
        starting_player_(starting_player),
        protagonist_player_(protagonist_player),
        state_space_(state_space) {

        var_mgr_ = game_.var_mgr();
        initial_eval_vector_ = var_mgr_->make_eval_vector(game_.automaton_id(), game_.initial_state());
        transition_compose_vector_ = var_mgr_->make_compose_vector(game_.automaton_id(),
                                                                   game_.transition_function());

        input_cube_ = var_mgr_->input_cube();
        output_cube_ = var_mgr_->output_cube();
        output_count_ = var_mgr_->output_variable_count();

        // Dump information about starting and protagonist players
        if (debug_enabled_) {
            std::cout << "[BuchiSolver INIT] starting_player="
                      << (starting_player_ == Player::Agent ? "Agent" : "Environment")
                      << " protagonist_player="
                      << (protagonist_player_ == Player::Agent ? "Agent" : "Environment")
                      << "\n";
        }

        std::tie(indep_quant_mode_, nonstate_quant_mode_) =
            compute_modes_for(starting_player_, protagonist_player_);
    }

    // mode-aware preimage
    CUDD::BDD BuchiSolver::preimage_with_modes(const CUDD::BDD &winning_states,
                                               IndepQuantMode indep_mode) const {
        // winning_transitions: transition-level BDD (may contain IO vars)
        CUDD::BDD winning_transitions = winning_states.VectorCompose(transition_compose_vector_);

        switch (indep_mode) {
            case IndepQuantMode::NO_QUANT:
                return winning_transitions;
            case IndepQuantMode::FORALL_INPUT:
                return winning_transitions.UnivAbstract(input_cube_);
            case IndepQuantMode::FORALL_OUTPUT:
                return winning_transitions.UnivAbstract(output_cube_);
            default:
                return winning_transitions;
        }
    }

    // default wrappers use solver's modes
    CUDD::BDD BuchiSolver::preimage(const CUDD::BDD &winning_states) const {
        return preimage_with_modes(winning_states, indep_quant_mode_);
    }

    bool BuchiSolver::includes_initial_state(const CUDD::BDD &winning_states) const {
        std::vector<int> tmp(initial_eval_vector_);
        return winning_states.Eval(tmp.data()).IsOne();
    }

CUDD::BDD BuchiSolver::CPre_agent(const CUDD::BDD& W_states) const {
    // Ensure we start from a pure-state target
    CUDD::BDD W = W_states & state_space_;

    // T(s,i,o) := next_state(s,i,o) ∈ W
    // This is a BDD over current STATE vars + IO vars (since we composed next-state bits).
    CUDD::BDD T = W.VectorCompose(transition_compose_vector_);

    CUDD::BDD pred;
    if (starting_player_ == Player::Environment) {
        // Env picks inputs first, Agent responds with outputs:
        //   ∀I ∃O : T
        // BDD quantifier order (inner first):
        //   pred = (∃O T) then ∀I
        pred = T.ExistAbstract(output_cube_).UnivAbstract(input_cube_);
    } else {
        // Agent picks outputs first, Env responds with inputs:
        //   ∃O ∀I : T
        // BDD quantifier order (inner first):
        //   pred = (∀I T) then ∃O
        pred = T.UnivAbstract(input_cube_).ExistAbstract(output_cube_);
    }

    // Result should now be a pure-state set; restrict to legal state space
    return pred & state_space_;
}

CUDD::BDD BuchiSolver::CPre_env(const CUDD::BDD& W_states) const {
    CUDD::BDD W = W_states & state_space_;
    CUDD::BDD T = W.VectorCompose(transition_compose_vector_);

    CUDD::BDD pred;
    if (starting_player_ == Player::Environment) {
        // Env picks inputs first, then Agent picks outputs:
        //   ∃I ∀O : T
        // BDD quantifier order:
        //   pred = (∀O T) then ∃I
        pred = T.UnivAbstract(output_cube_).ExistAbstract(input_cube_);
    } else {
        // Agent picks outputs first, then Env picks inputs:
        //   ∀O ∃I : T
        // BDD quantifier order:
        //   pred = (∃I T) then ∀O
        pred = T.ExistAbstract(input_cube_).UnivAbstract(output_cube_);
    }

    return pred & state_space_;
}
    CUDD::BDD BuchiSolver::computeCPreForPlayer(Player player, const CUDD::BDD &states) const {
        if (player == Player::Agent) {
            return CPre_agent(states);
        } else {
            return CPre_env(states);
        }
    }


    std::pair<CUDD::BDD, CUDD::BDD> BuchiSolver::computeAttr(const CUDD::BDD &region) const {
        auto mgr = var_mgr_->cudd_mgr();
        // start from the target region, ensure pure-state space
        CUDD::BDD W = region & state_space_;
        CUDD::BDD prev = mgr->bddZero();

        // iterate W := W ∪ CPre_protagonist(W) until fixed point
        while (!(W == prev)) {
            prev = W;
            CUDD::BDD pre = computeCPreForPlayer(protagonist_player_, W);
            W = W | pre;
        }

        // compute the move-level representation (transitions) that lead into W
        CUDD::BDD moves = preimage(W);

        return std::make_pair(W & state_space_, moves);
    }



    // recurrence construction (Finkbeiner variant); Fn and Wn are pure-state sets
    CUDD::BDD BuchiSolver::computeRecurrence(const CUDD::BDD &F_orig) const {
               // std::cout << "[BuchiSolver] computeRecurrence: start\n";
    // Dump the DFA in the Python-friendly text format for debugging/inspection
    if (debug_enabled_) DumpDFAForPython();

        CUDD::BDD F0 = F_orig;
        CUDD::BDD Fn = F0;

        // initial Wn = V \ Attr0(Fn)
        CUDD::BDD attr0 = computeAttr(Fn).first;
        CUDD::BDD Wn = state_space_ & (!attr0);

        Player opponent = (protagonist_player_ == Player::Agent) ? Player::Environment : Player::Agent;
        int iter = 0;
        while (true) {
            // compute opponent's controllable predecessor properly using owner-aware CPre
            CUDD::BDD Cpre1_Wn = computeCPreForPlayer(opponent, Wn); // pure-state set

            // F_{n+1} = F0 \ CPre1(Wn) (Finkbeiner variant); then normalize
            CUDD::BDD Fn1 = (F0 & (!Cpre1_Wn));

            // W_{n+1} = V \ Attr0(F_{n+1})
            CUDD::BDD attr1 = computeAttr(Fn).first;
            CUDD::BDD Wn1 = (!attr1);
            if (debug_enabled_) {
                std::cout << "[BuchiSolver] computeRecurrence iter=" << iter
                          << " Fn=" << Fn
                          << " Wn=" << Wn
                          << " CPre1_Wn=" << Cpre1_Wn
                          << " Fn1=" << Fn1
                          << " Attr(Fn1)=" << attr1
                          << " Wn1=" << Wn1
                          << "\n";

                std::cout << "[BuchiSolver] computeRecurrence iter=" << iter << " (detailed)\n";
                print_state_set(Fn, "Fn (current)");
                print_state_set(Wn, "Wn (current)");
                print_state_set(Cpre1_Wn, "CPre1_Wn (computed)");
                print_state_set(Fn1, "Fn1 (next)");
                print_state_set(attr1, "Attr(Fn1)");
                print_state_set(Wn1, "Wn1 (next)");
            }

            if (Fn1 == Fn && Wn1 == Wn) {
                if (debug_enabled_) std::cout << "[BuchiSolver] computeRecurrence: stabilized at iter=" << iter
                          << " RecurF=" << Fn1 << "\n";

                return Fn1;
            }

            Fn = Fn1;
            Wn = Wn1;
            ++iter;
        }
    }

    // Main run
    SynthesisResult BuchiSolver::run() {
                if (debug_enabled_) std::cout << "[BuchiSolver] run: starting solver\n";

        SynthesisResult result;
print_automaton_summary();

// If possible, print full set of all states in the state_space_ too
print_state_set(state_space_, "STATE_SPACE");
print_state_set(game_.final_states(), "INITIAL FINAL_STATES (game_.final_states())");

        // F 
        CUDD::BDD F = game_.final_states();
    if (debug_enabled_) std::cout << "[BuchiSolver] run: initial F = " << F << "\n";

        // compute recurrence
        CUDD::BDD RecurF = computeRecurrence(F);
    if (debug_enabled_) std::cout << "[BuchiSolver] run: RecurF = " << RecurF << "\n";

        // winning region = Attr0(Recur(F))
        auto [win_states, win_moves] = computeAttr(RecurF);
    if (debug_enabled_) std::cout << "[BuchiSolver] run: winning states = " << win_states << "\n";

        // normalize output states
        CUDD::BDD norm_win_states = win_states;

        if (includes_initial_state(norm_win_states)) {
            result.realizability = true;
            result.winning_states = norm_win_states;
            result.winning_moves = win_moves;
            result.transducer = nullptr;
            return result;
        } else {
            result.realizability = false;
            result.winning_states = norm_win_states;
            result.winning_moves = win_moves;
            result.transducer = nullptr;
            return result;
        }
    }

} // namespace Syft
// ...existing code...
    // Dump the underlying DFA in the plain text format expected by the Python loader.
    // Format markers: ===PYDFA_BEGIN=== ... ===PYDFA_END===
    void Syft::BuchiSolver::DumpDFAForPython() const {
        auto mgr = var_mgr_->cudd_mgr();
        auto automaton_id = game_.automaton_id();
        size_t num_state_bits = var_mgr_->get_state_variables(automaton_id).size();
        if (!debug_enabled_) return;
        auto state_vars = var_mgr_->get_state_variables(automaton_id);
        auto transition_func = game_.transition_function();
        size_t num_inputs = var_mgr_->input_variable_count();
        size_t num_outputs = var_mgr_->output_variable_count();

        std::cout << "===PYDFA_BEGIN===" << std::endl;
        std::cout << "num_state_bits=" << num_state_bits << std::endl;
        std::cout << "num_inputs=" << num_inputs << std::endl;
        std::cout << "num_outputs=" << num_outputs << std::endl;

        // State variable indices
        std::cout << "state_var_indices=";
        for (size_t i = 0; i < state_vars.size(); ++i) {
            if (i > 0) std::cout << ",";
            std::cout << state_vars[i].NodeReadIndex();
        }
        std::cout << std::endl;

        // Input variable labels and indices (if available)
        bool printed = false;
        try {
            auto input_labels = var_mgr_->input_variable_labels();
            std::cout << "input_labels=";
            for (size_t i = 0; i < input_labels.size(); ++i) {
                if (i > 0) std::cout << ",";
                std::cout << input_labels[i];
            }
            std::cout << std::endl;
            printed = true;
        } catch (...) {
            // fallthrough if labels not available
        }
        if (!printed) {
            std::cout << "input_labels=" << std::endl;
        }

        printed = false;
        try {
            auto output_labels = var_mgr_->output_variable_labels();
            std::cout << "output_labels=";
            for (size_t i = 0; i < output_labels.size(); ++i) {
                if (i > 0) std::cout << ",";
                std::cout << output_labels[i];
            }
            std::cout << std::endl;
            printed = true;
        } catch (...) {
            // fallthrough
        }
        if (!printed) {
            std::cout << "output_labels=" << std::endl;
        }

        // Guard against too many variables for brute-force enumeration
        size_t total_vars = num_state_bits + num_inputs + num_outputs;
        if (total_vars >= sizeof(unsigned long long) * 8) {
            std::cout << "[DumpDFAForPython] too many total vars for full enumeration: " << total_vars << std::endl;
            std::cout << "===PYDFA_END===" << std::endl;
            return;
        }

        uint64_t num_assignments = 1ULL << total_vars;

        for (size_t bit = 0; bit < transition_func.size(); ++bit) {
            std::cout << "trans_func_" << bit << "=";

            bool first = true;
            for (uint64_t assign = 0; assign < num_assignments; ++assign) {
                // Extract state, input, output from assignment bitfields
                uint64_t state_val = assign & ((1ULL << num_state_bits) - 1);
                uint64_t input_val = (assign >> num_state_bits) & ((1ULL << num_inputs) - 1);
                uint64_t output_val = (assign >> (num_state_bits + num_inputs)) & ((1ULL << num_outputs) - 1);

                // Build the assignment BDD
                CUDD::BDD assignment = mgr->bddOne();

                // State variables (use state_vars BDDs)
                for (size_t i = 0; i < num_state_bits; ++i) {
                    if ((state_val >> i) & 1ULL) assignment &= state_vars[i];
                    else assignment &= !state_vars[i];
                }

                // Inputs: assume BDD manager ordering for IO vars (fallback approach)
                for (size_t i = 0; i < num_inputs; ++i) {
                    CUDD::BDD var = mgr->bddVar(static_cast<int>(i));
                    if ((input_val >> i) & 1ULL) assignment &= var;
                    else assignment &= !var;
                }

                // Outputs
                for (size_t i = 0; i < num_outputs; ++i) {
                    CUDD::BDD var = mgr->bddVar(static_cast<int>(num_inputs + i));
                    if ((output_val >> i) & 1ULL) assignment &= var;
                    else assignment &= !var;
                }

                // If transition function bit is true under this assignment, record it
                if (!(transition_func[bit] & assignment).IsZero()) {
                    if (!first) std::cout << ";";
                    std::cout << state_val << "," << input_val << "," << output_val;
                    first = false;
                }
            }
            std::cout << std::endl;
        }

        // Dump accepting states as minterms
        std::cout << "accepting_minterms=";
        uint64_t num_states = 1ULL << num_state_bits;
        bool first_m = true;
        auto accepting = game_.final_states();
        for (uint64_t s = 0; s < num_states; ++s) {
            CUDD::BDD state_bdd = mgr->bddOne();
            for (size_t i = 0; i < num_state_bits; ++i) {
                if ((s >> i) & 1ULL) state_bdd &= state_vars[i];
                else state_bdd &= !state_vars[i];
            }
            if (!(state_bdd & accepting).IsZero()) {
                if (!first_m) std::cout << ";";
                for (size_t i = 0; i < num_state_bits; ++i) {
                    std::cout << (((s >> i) & 1ULL) ? '1' : '0');
                }
                first_m = false;
            }
        }
        std::cout << std::endl;

        // Dump initial state minterm (first matching)
        std::cout << "initial_minterm=";
        // SymbolicStateDfa::initial_state() returns a vector<int> of state bits.
        auto initial_bits = game_.initial_state();
        CUDD::BDD initial_bdd = mgr->bddOne();
        for (size_t i = 0; i < initial_bits.size(); ++i) {
            if (initial_bits[i]) initial_bdd &= state_vars[i];
            else initial_bdd &= !state_vars[i];
        }

        for (uint64_t s = 0; s < num_states; ++s) {
            CUDD::BDD state_bdd = mgr->bddOne();
            for (size_t i = 0; i < num_state_bits; ++i) {
                if ((s >> i) & 1ULL) state_bdd &= state_vars[i];
                else state_bdd &= !state_vars[i];
            }
            if (!(state_bdd & initial_bdd).IsZero()) {
                for (size_t i = 0; i < num_state_bits; ++i) {
                    std::cout << (((s >> i) & 1ULL) ? '1' : '0');
                }
                break;
            }
        }
        std::cout << std::endl;

        std::cout << "===PYDFA_END===" << std::endl;
    }
// ...existing code...