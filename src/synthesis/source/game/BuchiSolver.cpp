#include "game/BuchiSolver.hpp"
#include "automata/SymbolicStateDfa.h"
#include <iostream>
#include <tuple>
#include <spdlog/spdlog.h>

namespace Syft
{

    // Helper: build the BDD that corresponds to a concrete state index (pure state BDD).
    // Uses the automaton's state variables (NOT next-state or IO vars).
    CUDD::BDD BuchiSolver::state_index_to_bdd(std::size_t index) const
    {
        auto mgr = var_mgr_->cudd_mgr();
        auto automaton_id = game_.automaton_id();
        auto state_vars = var_mgr_->get_state_variables(automaton_id);
        std::size_t bit_count = state_vars.size();

        CUDD::BDD b = mgr->bddOne();
        for (std::size_t i = 0; i < bit_count; ++i)
        {
            bool bit = ((index >> i) & 1ull);
            if (bit)
                b *= state_vars[i];
            else
                b *= !state_vars[i];
        }
        return b;
    }
    // Pretty-print a pure-state BDD by enumerating concrete state indices present in it.
    // Safety: only enumerates if bit_count <= max_enum_bits (default 20).
    void BuchiSolver::print_state_set(const CUDD::BDD &set_bdd, const std::string &label, int max_enum_bits = 20) const
    {
        auto automaton_id = game_.automaton_id();
        auto state_vars = var_mgr_->get_state_variables(automaton_id);
        std::size_t bit_count = state_vars.size();

        if (!debug_enabled_)
            return;

            spdlog::trace("[BuchiSolver PRINT] {} nodes={} isZero={} isOne={}", label, set_bdd.nodeCount(), set_bdd.IsZero(), set_bdd.IsOne());

        if (bit_count > (std::size_t)max_enum_bits)
        {
            std::cout << "[BuchiSolver PRINT] skipping enumeration (bit_count=" << bit_count
                      << " > " << max_enum_bits << ")\n";
            return;
        }

        uint64_t total = 1ull << bit_count;
        std::vector<std::size_t> members;
        members.reserve(std::min<uint64_t>(total, 256));
        for (uint64_t s = 0; s < total; ++s)
        {
            CUDD::BDD state_bdd = state_index_to_bdd(s);
            // membership test: check if (state_bdd & set_bdd) is non-zero
            if (!((state_bdd & set_bdd).IsZero()))
            {
                members.push_back(static_cast<std::size_t>(s));
                if (members.size() >= 256)
                    break;
            }
        }

        std::cout << "[BuchiSolver PRINT] " << label << " members (count=" << members.size() << "): ";
        for (auto m : members)
            std::cout << m << " ";
        if (members.size() == 256)
            std::cout << "...";
        std::cout << "\n";
    }

    // Print a compact summary of the automaton: number of state bits and the raw BDDs
    void BuchiSolver::print_automaton_summary() const
    {
        auto automaton_id = game_.automaton_id();
        auto state_vars = var_mgr_->get_state_variables(automaton_id);
        std::size_t bit_count = state_vars.size();
        if (!debug_enabled_)
            return;

        std::cout << "[BuchiSolver PRINT] Automaton id=" << automaton_id
                  << " state_bits=" << bit_count
                  << " transition_funcs=" << game_.transition_function().size()
                  << " final_states_nodes=" << game_.final_states().nodeCount()
                  << "\n";

        // print final states BDD
        std::cout << "[BuchiSolver PRINT] final_states BDD = " << game_.final_states() << "\n";

        // print each transition function (bit-level) up to a reasonable number
        auto tfs = game_.transition_function();
        for (std::size_t i = 0; i < tfs.size(); ++i)
        {
            std::cout << "[BuchiSolver PRINT] transition bit " << i << " nodes=" << tfs[i].nodeCount()
                      << " bdd=" << tfs[i] << "\n";
        }
    }

    BuchiSolver::BuchiSolver(
        const SymbolicStateDfa &spec,
        Player starting_player,
        Player protagonist_player,
        const CUDD::BDD &state_space,
        BuchiSolver::BuchiMode mode) : game_(spec),
                                       starting_player_(starting_player),
                                       protagonist_player_(protagonist_player),
                                       state_space_(state_space),
                                       buechi_mode_(mode)
    {

        var_mgr_ = game_.var_mgr();
        initial_eval_vector_ = var_mgr_->make_eval_vector(game_.automaton_id(), game_.initial_state());
        transition_compose_vector_ = var_mgr_->make_compose_vector(game_.automaton_id(),
                                                                   game_.transition_function());

        input_cube_ = var_mgr_->input_cube();
        output_cube_ = var_mgr_->output_cube();
        output_count_ = var_mgr_->output_variable_count();
        if (starting_player_ == Player::Environment)
        {
            if (protagonist_player_ == Player::Environment)
            {
                quantify_independent_variables_ = std::make_unique<Forall>(output_cube_);
                quantify_non_state_variables_ = std::make_unique<Exists>(input_cube_);
            }
            else
            {
                quantify_independent_variables_ = std::make_unique<NoQuantification>();
                quantify_non_state_variables_ = std::make_unique<ForallExists>(input_cube_,
                                                                               output_cube_);
            }
        }
        else
        {
            if (protagonist_player_ == Player::Environment)
            {
                quantify_independent_variables_ = std::make_unique<NoQuantification>();
                quantify_non_state_variables_ = std::make_unique<ForallExists>(output_cube_,
                                                                               input_cube_);
            }
            else
            {
                quantify_independent_variables_ = std::make_unique<Forall>(input_cube_);
                quantify_non_state_variables_ = std::make_unique<Exists>(output_cube_);
            }
        }

        // Dump information about starting and protagonist players
        if (debug_enabled_)
        {
                spdlog::debug("[BuchiSolver INIT] starting_player={} protagonist_player={}",
                              (starting_player_ == Player::Agent ? "Agent" : "Environment"),
                              (protagonist_player_ == Player::Agent ? "Agent" : "Environment"));

            // print chosen Buchi mode
            std::string mode_str;
            switch (buechi_mode_)
            {
            case BuchiMode::PITERMAN:
                mode_str = "PITERMAN";
                break;
            case BuchiMode::COBUCHI:
                mode_str = "COBUCHI";
                break;
            default:
                mode_str = "CLASSIC";
                break;
            }
            spdlog::debug("[BuchiSolver INIT] mode={}", mode_str);
        }

        // game_.dump_json("buchi_dfa.json");
    }

    bool BuchiSolver::includes_initial_state(const CUDD::BDD &winning_states) const
    {
        std::vector<int> tmp(initial_eval_vector_);
        return winning_states.Eval(tmp.data()).IsOne();
    }

    CUDD::BDD BuchiSolver::CPre_agent(const CUDD::BDD &W_states) const
    {
        // Ensure we start from a pure-state target
        CUDD::BDD W = W_states & state_space_;

        // T(s,i,o) := next_state(s,i,o) ∈ W
        // This is a BDD over current STATE vars + IO vars (since we composed next-state bits).
        CUDD::BDD T = W.VectorCompose(transition_compose_vector_);

        // Step 1: Quantify independent variables (preimage)
        CUDD::BDD moves = quantify_independent_variables_->apply(T);
        // Step 2: Project into state space (eliminate IO vars)
        CUDD::BDD pred = quantify_non_state_variables_->apply(moves);
        // Step 3: Restrict to legal state space
        return pred & state_space_;
    }

    CUDD::BDD BuchiSolver::CPre_env(const CUDD::BDD &W_states) const
    {
        CUDD::BDD W = W_states & state_space_;
        CUDD::BDD T = W.VectorCompose(transition_compose_vector_);

        // Step 1: Quantify independent variables (preimage)
        CUDD::BDD moves = quantify_independent_variables_->apply(T);
        // Step 2: Project into state space (eliminate IO vars)
        CUDD::BDD pred = quantify_non_state_variables_->apply(moves);
        // Step 3: Restrict to legal state space
        return pred & state_space_;
    }
    CUDD::BDD BuchiSolver::computeCPreForPlayer(Player player, const CUDD::BDD &states) const
    {
        if (player == Player::Agent)
        {
            return CPre_agent(states);
        }
        else
        {
            return CPre_env(states);
        }
    }

    // Alternating safety / reachability algorithm
    // 1. W0 = empty
    // 2. Safety step: W_{i+1} = GFP X. (F ∪ W_i) ∩ CPre(X)
    // 3. Reachability step: W_{i+1} = LFP X. W_i ∪ CPre(X)
    // 4. If W_i == W_{i-2} terminate else repeat
    CUDD::BDD BuchiSolver::alternatingSafetyReachability() const
    {
        auto mgr = var_mgr_->cudd_mgr();
        CUDD::BDD F = game_.final_states() & state_space_;

        // W = empty
        CUDD::BDD W = mgr->bddZero();

        int outer_iter = 0;
        while (true)
        {
            outer_iter++;

            // Safety GFP: X, XX = BDD.one; iterate XX = (F || W) && Cpre(X)
            CUDD::BDD X = mgr->bddOne();
            CUDD::BDD XX = mgr->bddOne();
            int safety_iters = 0;
            while (true)
            {
                safety_iters++;
                XX = ((F | W) & computeCPreForPlayer(protagonist_player_, X)) & state_space_;
                if (XX == X)
                    break;
                X = XX;
            }

            if (debug_enabled_)
            {
                spdlog::debug("[BuchiSolver Alternating] outer={} safety_iters={} X_nodes={}", outer_iter, safety_iters, X.nodeCount());
                //print_state_set(X, "X (after safety)");
            }

            if (W == X)
            {
                if (debug_enabled_)
                    spdlog::debug("[BuchiSolver Alternating] W==X, terminating at outer={}", outer_iter);
                return W & state_space_;
            }
            else
            {
                W = X & state_space_;
            }

            // Reachability LFP: Y, YY = BDD.zero; iterate YY = W || Cpre(Y)
            CUDD::BDD Y = mgr->bddZero();
            CUDD::BDD YY = mgr->bddZero();
            int reach_iters = 0;
            while (true)
            {
                reach_iters++;
                YY = (W | computeCPreForPlayer(protagonist_player_, Y)) & state_space_;
                if (YY == Y)
                    break;
                Y = YY;
            }

            if (debug_enabled_)
            {
                spdlog::debug("[BuchiSolver Alternating] outer={} reach_iters={} Y_nodes={}", outer_iter, reach_iters, Y.nodeCount());
                //print_state_set(Y, "Y (after reach)");
            }

            if (W == Y)
            {
                if (debug_enabled_)
                    spdlog::debug("[BuchiSolver Alternating] W==Y, terminating at outer={}", outer_iter);
                return W & state_space_;
            }
            else
            {
                W = Y & state_space_;
            }
        }
    }

    bool BuchiSolver::DoubleFixpoint()
    {
        // Compute nu X. mu Y. (F ∩ CPre_s(X)) ∪ CPre_s(Y)
        auto mgr = var_mgr_->cudd_mgr();

        // Initialize X to the whole state space (greatest fixpoint start)
        CUDD::BDD X = mgr->bddOne();
        CUDD::BDD prevX = mgr->bddZero();

        int outer_iter = 0;
        while (!(X == prevX))
        {
            prevX = X;
            outer_iter++;

            // Inner least fixpoint: Y_{k+1} = (F ∩ CPre_s(X)) ∪ CPre_s(Y_k) ∪ Y_k
            CUDD::BDD Y = mgr->bddZero();
            CUDD::BDD prevY; // assigned inside loop
            int inner_iter = 0;

            // Precompute the term F ∩ CPre_s(X) which is constant during inner loop
            CUDD::BDD FcpreX = game_.final_states() & computeCPreForPlayer(protagonist_player_, X);

            // Use a do/while so the inner least-fixpoint iterates at least once.
            do
            {
                prevY = Y;
                inner_iter++;

                // Add union with previous Y (target) as in EL solver
                CUDD::BDD newY = (FcpreX | computeCPreForPlayer(protagonist_player_, Y)) | Y;
                // keep within state space
                Y = newY & state_space_;
                if (debug_enabled_)
            {
                 spdlog::info("[BuchiSolver DoubleFixpoint] inner_iter={}", inner_iter);
                //(X, "X (current)");
            }
            } while (!(Y == prevY));
                spdlog::info("[BuchiSolver DoubleFixpoint] inner finished");

            // The phi(X) is the inner fixpoint Y
            X = Y & state_space_;

            if (debug_enabled_)
            {
                spdlog::info("[BuchiSolver DoubleFixpoint] outer_iter={}, inner_iters={}, X_nodes={}", outer_iter, inner_iter, X.nodeCount());
                //print_state_set(X, "X (current)");
            }
        }

        // Return whether initial state is contained in the outer fixpoint X
        CUDD::BDD initial = game_.initial_state_bdd();
        bool initial_in = (initial & !X).IsZero();
        if (debug_enabled_)
                spdlog::debug("[BuchiSolver DoubleFixpoint] initial_in={}", initial_in);
        return initial_in;
    }

    // Main run
    SynthesisResult BuchiSolver::run()
    {
        if (debug_enabled_)
            spdlog::info("[BuchiSolver] run: starting solver");

        SynthesisResult result;
        print_automaton_summary();

        // If possible, print full set of all states in the state_space_ too
        // print_state_set(state_space_, "STATE_SPACE");
        // print_state_set(game_.final_states(), "INITIAL FINAL_STATES (game_.final_states())");

        // F
        CUDD::BDD F = game_.final_states();
        if (debug_enabled_)
            spdlog::info("[BuchiSolver] run: initial F = {}", F);

        // normalize output states
        CUDD::BDD norm_win_states;
        CUDD::BDD win_moves;
        bool wining = false;

        if (buechi_mode_ == BuchiMode::PITERMAN)
        {   
            spdlog::info("[BuchiSolver] run: using Piterman mode");
            // Use Piterman's alternating safety/reachability construction
            norm_win_states = alternatingSafetyReachability();
            win_moves = CUDD::BDD();

            // check if initial state is in the computed winning region
            wining = includes_initial_state(norm_win_states);
        }
        else if (buechi_mode_ == BuchiMode::COBUCHI)
        {
            spdlog::info("[BuchiSolver] run: using CoBuchi mode");
            norm_win_states = CoBuchiFixpoint();
            win_moves = CUDD::BDD();
            wining = includes_initial_state(norm_win_states);
        }
        else
        {
            // Classic double-fixpoint mode
                        spdlog::info("[BuchiSolver] run: using Classic mode");
            
            norm_win_states = CUDD::BDD();
            win_moves = CUDD::BDD();
            wining = DoubleFixpoint();
        }
        result.realizability = wining;
        result.winning_states = norm_win_states;
        result.winning_moves = win_moves;
        result.transducer = nullptr;
        return result;
    }

    // CoBuchi fixed point algorithm
    CUDD::BDD BuchiSolver::CoBuchiFixpoint() const
    {
        auto mgr = var_mgr_->cudd_mgr();
        CUDD::BDD F = game_.final_states() & state_space_;

        // Outer fixpoint: X, XX = BDD.zero
        CUDD::BDD X = mgr->bddZero();
        CUDD::BDD XX = mgr->bddZero();

        while (true)
        {
            // Inner fixpoint: Y, YY = BDD.one
            CUDD::BDD Y = mgr->bddOne();
            CUDD::BDD YY = mgr->bddOne();
            while (true)
            {
                // YY = F && CPre(Y) || CPre(X)
                YY = ((F & computeCPreForPlayer(protagonist_player_, Y)) | computeCPreForPlayer(protagonist_player_, X)) & state_space_;
                if (YY == Y)
                    break;
                Y = YY;
            }
            if (Y == X)
                break;
            X = Y;
        }
        return X & state_space_;
    }

} // namespace Syft
