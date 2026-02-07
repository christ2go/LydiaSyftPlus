// ObligationLTLfPlusSynthesizer.cpp
#include "synthesizer/ObligationLTLfPlusSynthesizer.h"
#include "automata/ExplicitStateDfa.h"
#include "automata/ExplicitStateDfaAdd.h"
#include "game/BuchiSolver.hpp"   // standalone Buchi solver (uses arena.final_states())
#include "game/WeakGameSolver.h"
#include "lydia/logic/ltlfplus/base.hpp"
#include "lydia/logic/pnf.hpp"
#include "lydia/utils/print.hpp"
#include "lydia/mona_ext/mona_ext_base.hpp"
#include "lydia/dfa/mona_dfa.hpp"
#include <stdexcept>
#include <vector>
#include <stack>
#include <cctype>
#include <sstream>
#include <algorithm>
#include <iostream>
#include <chrono>
#include <optional>
#include <utility>
#include <functional>
#include <boost/multiprecision/cpp_int.hpp>

namespace {
    using BigInt = boost::multiprecision::cpp_int;

    std::string bigint_to_string(const std::optional<BigInt>& value) {
        if (!value.has_value()) {
            return "unknown";
        }
        std::ostringstream oss;
        oss << value.value();
        return oss.str();
    }
}

namespace Syft {

    ObligationLTLfPlusSynthesizer::ObligationLTLfPlusSynthesizer(
        LTLfPlus ltlf_plus_formula,
        InputOutputPartition partition,
        Player starting_player,
        Player protagonist_player,
                bool use_buchi,
                Syft::BuchiSolver::BuchiMode buechi_mode,
                MinimisationOptions minimisation_options,
                bool use_balanced_boolean_product)
        : ltlf_plus_formula_(ltlf_plus_formula),
          starting_player_(starting_player),
          protagonist_player_(protagonist_player),
                    use_buchi_(use_buchi),
                    minimisation_options_(minimisation_options),
                    use_balanced_boolean_product_(use_balanced_boolean_product) {
        buechi_mode_ = buechi_mode;
        std::shared_ptr<VarMgr> var_mgr = std::make_shared<VarMgr>();
        var_mgr->create_named_variables(partition.input_variables);
        var_mgr->create_named_variables(partition.output_variables);

        var_mgr->partition_variables(partition.input_variables,
                                     partition.output_variables);
        var_mgr_ = var_mgr;
    }

    void ObligationLTLfPlusSynthesizer::validate_obligation_fragment() const {
        // Check each subformula to ensure it's in the obligation fragment
        for (const auto& [formula, color] : ltlf_plus_formula_.formula_to_color_) {
            // Convert back to ltlf_plus_ptr for the detector
            // We need to check if the quantifier is obligation-compatible
            auto quantifier = ltlf_plus_formula_.formula_to_quantification_.at(formula);
            
            // Only Forall and Exists are allowed in obligation fragment
            if (quantifier != whitemech::lydia::PrefixQuantifier::Forall &&
                quantifier != whitemech::lydia::PrefixQuantifier::Exists) {
                std::string error_msg = "Formula is not in obligation fragment. Found quantifier: ";
                switch (quantifier) {
                    case whitemech::lydia::PrefixQuantifier::ForallExists:
                        error_msg += "ForallExists (recurrence)";
                        break;
                    case whitemech::lydia::PrefixQuantifier::ExistsForall:
                        error_msg += "ExistsForall (persistence)";
                        break;
                    default:
                        error_msg += "Unknown";
                        break;
                }
                throw std::runtime_error(error_msg);
            }
        }
    }

    // Threshold for switching from explicit to symbolic representation
    // 2^8 = 256 states is a reasonable threshold
    static constexpr int EXPLICIT_TO_SYMBOLIC_THRESHOLD = 256;
    // Maxmimum number of states to start minimisation
    static constexpr int MINIMISATION_THRESHOLD = 0;  // Disabled for now

    // Helper struct to hold either explicit or symbolic DFA
    struct HybridDfa {
    std::optional<ExplicitStateDfa> explicit_dfa;
    std::optional<SymbolicStateDfa> symbolic_dfa;
    std::optional<BigInt> approx_state_count; // Optional approximation of number of states
        bool is_symbolic;
        std::shared_ptr<VarMgr> var_mgr;
        
        // Constructor from explicit DFA
                                HybridDfa(ExplicitStateDfa e, std::shared_ptr<VarMgr> vm) 
                                        : explicit_dfa(std::move(e)), symbolic_dfa(std::nullopt), 
                                            is_symbolic(false), var_mgr(vm) {
                                                approx_state_count = BigInt(explicit_dfa->get_nb_states());
              }
        
        // Constructor from symbolic DFA (already converted)
        HybridDfa(const SymbolicStateDfa& s, std::shared_ptr<VarMgr> vm)
            : explicit_dfa(std::nullopt), symbolic_dfa(s), 
              is_symbolic(true), var_mgr(vm) {
              }
        
        std::optional<BigInt> state_count() const {
            if (!is_symbolic) {
                return BigInt(explicit_dfa->dfa_->ns);
            }
            return approx_state_count;
        }

        std::string state_count_str() const {
            return bigint_to_string(state_count());
        }

        void set_state_count(const BigInt& value) {
            approx_state_count = value;
        }

        void clear_state_count() {
            approx_state_count.reset();
        }
        
        void convert_to_symbolic_if_needed() {
            if (!is_symbolic && explicit_dfa->dfa_->ns > EXPLICIT_TO_SYMBOLIC_THRESHOLD) {
                spdlog::debug("[ObligationFragment] Converting to symbolic (exceeded threshold: {} > {})",
                              explicit_dfa->dfa_->ns, EXPLICIT_TO_SYMBOLIC_THRESHOLD);
                BigInt explicit_count = BigInt(explicit_dfa->get_nb_states());
                symbolic_dfa = SymbolicStateDfa::from_explicit(
                    ExplicitStateDfaAdd::from_dfa_mona(var_mgr, *explicit_dfa));
                is_symbolic = true;
                approx_state_count = explicit_count;
                explicit_dfa = std::nullopt;  // Free the explicit DFA
            }
        }
        
        SymbolicStateDfa to_symbolic() {
            if (!is_symbolic) {
                BigInt explicit_count = BigInt(explicit_dfa->get_nb_states());
                symbolic_dfa = SymbolicStateDfa::from_explicit(
                    ExplicitStateDfaAdd::from_dfa_mona(var_mgr, *explicit_dfa));
                is_symbolic = true;
                approx_state_count = explicit_count;
                explicit_dfa = std::nullopt;  // Free the explicit DFA
            }
            return *symbolic_dfa;
        }
    };

    // Helper to parse color formula and build arena using hybrid approach
    // Starts with explicit DFAs, automatically switches to symbolic when threshold exceeded
    SymbolicStateDfa ObligationLTLfPlusSynthesizer::build_arena_from_color_formula_hybrid(
        const std::string& color_formula,
        const std::map<int, ExplicitStateDfa>& color_to_dfa) const {
        
        // Parse the color formula (e.g., "(1 & 2) | 3") and build DFA product using hybrid approach
        // Simple recursive descent parser for: expr = term (('|' term)*)
        //                                      term = factor (('&' factor)*)
        //                                      factor = number | '(' expr ')'
        
        std::string formula = color_formula;
        size_t pos = 0;
        auto var_mgr = var_mgr_;  // Capture var_mgr for lambdas
        
        std::function<HybridDfa()> parse_expr;
        std::function<HybridDfa()> parse_term;
        std::function<HybridDfa()> parse_factor;

        auto skip_whitespace = [&]() {
            while (pos < formula.size() && std::isspace(static_cast<unsigned char>(formula[pos]))) {
                ++pos;
            }
        };

        auto combine_pair = [&](HybridDfa left, HybridDfa right, bool is_or) -> HybridDfa {
            auto left_est = left.state_count();
            auto right_est = right.state_count();
            // Also switch to symbolic if product estimate exceeds threshold
            auto estimated_product = std::optional<BigInt>(1);
            if (left_est.has_value()) {
                estimated_product.value() *= left_est.value();
            } else {
                estimated_product = std::nullopt;
            }
            if (right_est.has_value()) {
                estimated_product.value() *= right_est.value();
            } else {
                estimated_product = std::nullopt;
            }
            if (left.is_symbolic || right.is_symbolic || (estimated_product.has_value() && estimated_product.value() > minimisation_options_.threshold)) {
                spdlog::debug("[ObligationFragment] Computing {} product using symbolic representation",
                              is_or ? "OR" : "AND");
                SymbolicStateDfa left_sym = left.to_symbolic();
                SymbolicStateDfa right_sym = right.to_symbolic();
                SymbolicStateDfa product = is_or
                    ? SymbolicStateDfa::product_OR({left_sym, right_sym})
                    : SymbolicStateDfa::product_AND({left_sym, right_sym});
                HybridDfa combined(product, var_mgr_);
                if (left_est && right_est) {
                    combined.set_state_count(left_est.value() * right_est.value());
                } else {
                    combined.clear_state_count();
                }
                spdlog::debug("[ObligationFragment] Symbolic {} product computed",
                              is_or ? "OR" : "AND");
                spdlog::debug(
                    "[ObligationFragment] {} product combined ~{} with ~{} -> ~{}",
                    is_or ? "OR" : "AND",
                    bigint_to_string(left_est),
                    bigint_to_string(right_est),
                    combined.state_count_str());
                return combined;
            }

            if (is_or) {
                spdlog::debug("[ObligationFragment] Computing OR product using MONA");
            } else {
                spdlog::debug("[ObligationFragment] Computing AND product using MONA");
            }

            ExplicitStateDfa product = is_or
                ? ExplicitStateDfa::dfa_product_or({*left.explicit_dfa, *right.explicit_dfa})
                : ExplicitStateDfa::dfa_product_and({*left.explicit_dfa, *right.explicit_dfa});
            spdlog::debug("[ObligationFragment] {} product has {} states",
                         is_or ? "OR" : "AND",
                         product.dfa_->ns);

            if (product.dfa_->ns < minimisation_options_.threshold && minimisation_options_.allow_minimisation) {
                spdlog::debug("[ObligationFragment] Minimizing {} product (states: {} < {})",
                             is_or ? "OR" : "AND",
                             product.dfa_->ns,
                             minimisation_options_.threshold);
                product = ExplicitStateDfa::dfa_minimize_weak(product);
            }

            HybridDfa combined(std::move(product), var_mgr_);
            combined.convert_to_symbolic_if_needed();

            spdlog::debug(
                "[ObligationFragment] {} product combined ~{} with ~{} -> ~{}",
                is_or ? "OR" : "AND",
                bigint_to_string(left_est),
                bigint_to_string(right_est),
                combined.state_count_str());

            return combined;
        };

        auto reduce_operands = [&](std::vector<HybridDfa>&& operands, bool is_or) -> HybridDfa {
            if (operands.empty()) {
                throw std::runtime_error("Empty operand list in color formula");
            }
            if (operands.size() == 1) {
                return std::move(operands.front());
            }
            std::vector<HybridDfa> current = std::move(operands);
            while (current.size() > 1) {
                std::vector<HybridDfa> next;
                next.reserve((current.size() + 1) / 2);
                std::size_t i = 0;
                for (; i + 1 < current.size(); i += 2) {
                    next.push_back(combine_pair(std::move(current[i]), std::move(current[i + 1]), is_or));
                }
                if (i < current.size()) {
                    next.push_back(std::move(current.back()));
                }
                current = std::move(next);
            }
            return std::move(current.front());
        };

        parse_factor = [&]() -> HybridDfa {
            skip_whitespace();
            if (pos >= formula.size()) {
                throw std::runtime_error("Unexpected end of color formula");
            }

            if (formula[pos] == '(') {
                ++pos;
                HybridDfa result = parse_expr();
                skip_whitespace();
                if (pos >= formula.size() || formula[pos] != ')') {
                    throw std::runtime_error("Expected ')' in color formula");
                }
                ++pos;
                return result;
            }
            if (std::isdigit(static_cast<unsigned char>(formula[pos]))) {
                size_t start = pos;
                while (pos < formula.size() && std::isdigit(static_cast<unsigned char>(formula[pos]))) {
                    ++pos;
                }
                int color = std::stoi(formula.substr(start, pos - start));

                auto it = color_to_dfa.find(color);
                if (it == color_to_dfa.end()) {
                    throw std::runtime_error("Unknown color in formula: " + std::to_string(color));
                }
                return HybridDfa(it->second, var_mgr_);
            }

            throw std::runtime_error("Unexpected character in color formula: " + std::string(1, formula[pos]));
        };

        parse_term = [&]() -> HybridDfa {
            std::vector<HybridDfa> factors;
            factors.push_back(parse_factor());
            while (true) {
                skip_whitespace();
                if (pos < formula.size() && formula[pos] == '&') {
                    ++pos;
                    factors.push_back(parse_factor());
                } else {
                    break;
                }
            }

            if (use_balanced_boolean_product_) {
                return reduce_operands(std::move(factors), false);
            }

            HybridDfa accum = std::move(factors.front());
            for (std::size_t idx = 1; idx < factors.size(); ++idx) {
                accum = combine_pair(std::move(accum), std::move(factors[idx]), false);
            }
            return accum;
        };

        parse_expr = [&]() -> HybridDfa {
            std::vector<HybridDfa> terms;
            terms.push_back(parse_term());
            while (true) {
                skip_whitespace();
                if (pos < formula.size() && formula[pos] == '|') {
                    ++pos;
                    terms.push_back(parse_term());
                } else {
                    break;
                }
            }

            if (use_balanced_boolean_product_) {
                return reduce_operands(std::move(terms), true);
            }

            HybridDfa accum = std::move(terms.front());
            for (std::size_t idx = 1; idx < terms.size(); ++idx) {
                accum = combine_pair(std::move(accum), std::move(terms[idx]), true);
            }
            return accum;
        };
        
        HybridDfa res = parse_expr();
        skip_whitespace();
        if (pos != formula.size()) {
            throw std::runtime_error("Trailing characters in color formula after parsing");
        }
        auto symbolic_arena = res.to_symbolic();
        // info_log the number of states we approximated using spdlog
        spdlog::info("[ObligationFragment] Final arena has approximately {} states, {} bits ",
                     res.state_count_str(),
                     symbolic_arena.transition_function().size());

        // Always return symbolic representation
        return symbolic_arena;
    }

    std::pair<SymbolicStateDfa, std::map<int, CUDD::BDD>> 
    ObligationLTLfPlusSynthesizer::convert_to_symbolic_dfa() const {
        using clock = std::chrono::high_resolution_clock;
        auto t0 = clock::now();
        
        // Step 1: Build all explicit DFAs with obligation transformations
        std::map<int, ExplicitStateDfa> color_to_explicit_dfa;
        std::map<int, CUDD::BDD> color_to_final_states;

        spdlog::info("[ObligationFragment] Building explicit DFAs for each color...");
        
        for (const auto& [ltlf_plus_arg, prefix_quantifier] : ltlf_plus_formula_.formula_to_quantification_) {
            whitemech::lydia::ltlf_ptr ltlf_arg = ltlf_plus_arg->ltlf_arg();
            ExplicitStateDfa explicit_dfa = ExplicitStateDfa::dfa_of_formula(*ltlf_arg);
            
            int color = std::stoi(ltlf_plus_formula_.formula_to_color_.at(ltlf_plus_arg));

            switch (prefix_quantifier) {
                case whitemech::lydia::PrefixQuantifier::Forall: {
                    // Safety property: convert to G(phi) form
                    spdlog::debug("[ObligationFragment] Applying Forall transformation for color {}", color);
                    ExplicitStateDfa trimmed_explicit_dfa = ExplicitStateDfa::dfa_to_Gdfa_obligation(explicit_dfa);
                    ExplicitStateDfa minised = ExplicitStateDfa::dfa_minimize_weak(trimmed_explicit_dfa);   
                    color_to_explicit_dfa.insert({color, std::move(minised)});
                    break;
                }
                case whitemech::lydia::PrefixQuantifier::Exists: {
                    // Guarantee property: convert to F(phi) form
                    spdlog::debug("[ObligationFragment] Applying Exists transformation for color {}", color);
                    ExplicitStateDfa trimmed_explicit_dfa = ExplicitStateDfa::dfa_to_Fdfa_obligation(explicit_dfa);
                    ExplicitStateDfa minised = ExplicitStateDfa::dfa_minimize_weak(trimmed_explicit_dfa);   
                    color_to_explicit_dfa.insert({color, std::move(minised)});
                    break;
                }
                default:
                    // This should not happen since validate_obligation_fragment was called
                    throw std::runtime_error("Unexpected quantifier in obligation fragment conversion");
            }
        }

        // Step 2: Build the product arena using hybrid approach (MONA when small, symbolic when large)
        spdlog::info("[ObligationFragment] Computing product DFA using hybrid approach...");
        SymbolicStateDfa arena = build_arena_from_color_formula_hybrid(
            ltlf_plus_formula_.color_formula_, color_to_explicit_dfa);
        
        spdlog::info("[ObligationFragment] Final arena DFA created");
        // Step 3: Collect final states for debugging (convert individual DFAs just for final state info)
        for (const auto &[color, explicit_dfa] : color_to_explicit_dfa) {
            ExplicitStateDfaAdd add = ExplicitStateDfaAdd::from_dfa_mona(var_mgr_, explicit_dfa);
            SymbolicStateDfa symbolic = SymbolicStateDfa::from_explicit(std::move(add));
            color_to_final_states[color] = symbolic.final_states();
        }

        // the arena already encodes the combined finals in arena.final_states()
        color_to_final_states[-1] = arena.final_states();
        
        auto t1 = clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
        spdlog::info("[ObligationFragment] Total DFA construction time: {} ms", ms);

        return std::make_pair(arena, color_to_final_states);
    }

    ELSynthesisResult ObligationLTLfPlusSynthesizer::solve_with_scc(
        const SymbolicStateDfa& arena,
        const std::map<int, CUDD::BDD>& color_to_final_states) const {
                    spdlog::info("[ObligationFragment] Solving with WeakGameSolver");
        
        // Use the arena's final states which already encode the correct AND/OR structure
        CUDD::BDD accepting_states = arena.final_states();
                
        // Create and run the weak game solver (debug=true for detailed output)
        WeakGameSolver solver(arena, accepting_states, true);
        WeakGameResult game_result = solver.Solve();
                
        // Check if initial state is winning
        CUDD::BDD initial_state = arena.initial_state_bdd();
        bool is_realizable = !(initial_state & !game_result.winning_states).IsZero() == false;
        // Simplified: check if initial state is in winning states
        is_realizable = !(initial_state & game_result.winning_states).IsZero();
        
        spdlog::info("[ObligationFragment] Realizability: {}", (is_realizable ? "true" : "false"));
        
        // Build result
        ELSynthesisResult result;
        result.realizability = is_realizable;
        result.winning_states = game_result.winning_states;
        result.output_function = {};  // TODO: Extract strategy from winning_moves
        result.z_tree = nullptr;
        
        return result;

        }        

    ELSynthesisResult ObligationLTLfPlusSynthesizer::solve_with_buchi(
        const SymbolicStateDfa& arena,
        const std::map<int, CUDD::BDD>& color_to_final_states) const {
        
        spdlog::info("[ObligationFragment] Solving with BuchiStandalone solver");
        
        // Use the arena's final states which already encode the correct AND/OR structure
        CUDD::BDD accepting_states = arena.final_states();
        
        auto var_mgr = arena.var_mgr();
        auto mgr = var_mgr->cudd_mgr();
        auto automaton_id = arena.automaton_id();
        auto transition_func = arena.transition_function();
        auto initial_state = arena.initial_state_bdd();
        
        CUDD::BDD state_space = initial_state;
        CUDD::BDD current_layer = initial_state;
        auto transition_vector = var_mgr->make_compose_vector(automaton_id, transition_func);
        CUDD::BDD io_cube = var_mgr->input_cube() * var_mgr->output_cube();
        
        
        // Create and run the Büchi solver (arena already has final_states)
    BuchiSolver solver(arena, starting_player_, protagonist_player_, var_mgr_->cudd_mgr()->bddOne(), buechi_mode_);
        SynthesisResult game_result = solver.run();
        
        spdlog::info("[ObligationFragment] BuchiStandalone completed");
        spdlog::info("[ObligationFragment] Realizability: {}", (game_result.realizability ? "true" : "false"));
        
        // Convert SynthesisResult to ELSynthesisResult
        ELSynthesisResult result;
        result.realizability = game_result.realizability;
        result.winning_states = game_result.winning_states;
        result.output_function = {};  // strategy extraction omitted
        result.z_tree = nullptr;      // not used here
        
        return result;
    }

    ELSynthesisResult ObligationLTLfPlusSynthesizer::run() const {
        // Step 1: Validate that the formula is in obligation fragment
        validate_obligation_fragment();
        
        // Step 2: Convert to symbolic state DFA
        auto [arena, color_to_final_states] = convert_to_symbolic_dfa();
        
        // Step 3: Solve using Büchi solver (replaces SCC/WeakGame path)
        if (use_buchi_) {
            return solve_with_buchi(arena, color_to_final_states);
        } else {
            return solve_with_scc(arena, color_to_final_states);
        }
    }

} // namespace Syft
