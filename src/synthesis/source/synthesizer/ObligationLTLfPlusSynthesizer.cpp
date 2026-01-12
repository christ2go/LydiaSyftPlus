// ObligationLTLfPlusSynthesizer.cpp
#include "synthesizer/ObligationLTLfPlusSynthesizer.h"
#include "automata/ExplicitStateDfa.h"
#include "automata/ExplicitStateDfaAdd.h"
#include "game/BuchiSolver.hpp"   // standalone Buchi solver (uses arena.final_states())
#include "lydia/logic/ltlfplus/base.hpp"
#include "lydia/logic/pnf.hpp"
#include "lydia/utils/print.hpp"
#include <stdexcept>
#include <vector>
#include <stack>
#include <cctype>
#include <sstream>
#include <algorithm>
#include <iostream>
#include <chrono>

namespace Syft {

    ObligationLTLfPlusSynthesizer::ObligationLTLfPlusSynthesizer(
        LTLfPlus ltlf_plus_formula,
        InputOutputPartition partition,
        Player starting_player,
        Player protagonist_player,
        Syft::BuchiSolver::BuchiMode buechi_mode)
        : ltlf_plus_formula_(ltlf_plus_formula),
          starting_player_(starting_player),
          protagonist_player_(protagonist_player) {
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

    // Helper to parse color formula and build DFA following the boolean structure
    SymbolicStateDfa ObligationLTLfPlusSynthesizer::build_arena_from_color_formula(
        const std::string& color_formula,
        const std::map<int, SymbolicStateDfa>& color_to_dfa) const {
        
        // Parse the color formula (e.g., "(1 & 2) | 3") and build DFA accordingly
        // Simple recursive descent parser for: expr = term (('|' term)*)
        //                                      term = factor (('&' factor)*)
        //                                      factor = number | '(' expr ')'
        
        std::string formula = color_formula;
        size_t pos = 0;
        
        std::function<SymbolicStateDfa()> parse_expr;
        std::function<SymbolicStateDfa()> parse_term;
        std::function<SymbolicStateDfa()> parse_factor;
        
        auto skip_whitespace = [&]() {
            while (pos < formula.size() && std::isspace(static_cast<unsigned char>(formula[pos]))) pos++;
        };
        
        parse_factor = [&]() -> SymbolicStateDfa {
            skip_whitespace();
            if (pos >= formula.size()) {
                throw std::runtime_error("Unexpected end of color formula");
            }
            
            if (formula[pos] == '(') {
                pos++;  // skip '('
                SymbolicStateDfa result = parse_expr();
                skip_whitespace();
                if (pos >= formula.size() || formula[pos] != ')') {
                    throw std::runtime_error("Expected ')' in color formula");
                }
                pos++;  // skip ')'
                return result;
            } else if (std::isdigit(static_cast<unsigned char>(formula[pos]))) {
                // Parse number
                size_t start = pos;
                while (pos < formula.size() && std::isdigit(static_cast<unsigned char>(formula[pos]))) pos++;
                int color = std::stoi(formula.substr(start, pos - start));
                
                auto it = color_to_dfa.find(color);
                if (it == color_to_dfa.end()) {
                    throw std::runtime_error("Unknown color in formula: " + std::to_string(color));
                }
                // Use a fresh copy so repeated colors don't alias state variables
                return it->second.clone_with_fresh_state_space();
            } else {
                throw std::runtime_error("Unexpected character in color formula: " + std::string(1, formula[pos]));
            }
        };
        
        parse_term = [&]() -> SymbolicStateDfa {
            SymbolicStateDfa left = parse_factor();
            
            while (true) {
                skip_whitespace();
                if (pos < formula.size() && formula[pos] == '&') {
                    pos++;  // skip '&'
                    SymbolicStateDfa right = parse_factor();
                    // AND product
                    left = SymbolicStateDfa::product_AND({left, right});
                    std::cout << "AND pruduct computed\n" << std::endl;
                } else {
                    break;
                }
            }
            return left;
        };
        
        parse_expr = [&]() -> SymbolicStateDfa {
            SymbolicStateDfa left = parse_term();
            
            while (true) {
                skip_whitespace();
                if (pos < formula.size() && formula[pos] == '|') {
                    pos++;  // skip '|'
                    SymbolicStateDfa right = parse_term();
                    // OR product
                    left = SymbolicStateDfa::product_OR({left, right});
                } else {
                    break;
                }
            }
            return left;
        };
        
        SymbolicStateDfa res = parse_expr();
        skip_whitespace();
        if (pos != formula.size()) {
            throw std::runtime_error("Trailing characters in color formula after parsing");
        }
        return res;
    }

    std::pair<SymbolicStateDfa, std::map<int, CUDD::BDD>> 
    ObligationLTLfPlusSynthesizer::convert_to_symbolic_dfa() const {
        using clock = std::chrono::high_resolution_clock;
        auto t0 = clock::now();
        std::map<int, SymbolicStateDfa> color_to_dfa;
        std::map<int, CUDD::BDD> color_to_final_states;

        for (const auto& [ltlf_plus_arg, prefix_quantifier] : ltlf_plus_formula_.formula_to_quantification_) {
            whitemech::lydia::ltlf_ptr ltlf_arg = ltlf_plus_arg->ltlf_arg();
            ExplicitStateDfa explicit_dfa = ExplicitStateDfa::dfa_of_formula(*ltlf_arg);

            switch (prefix_quantifier) {
                case whitemech::lydia::PrefixQuantifier::Forall: {
                    // Safety property: convert to G(phi) form
                    ExplicitStateDfa trimmed_explicit_dfa = ExplicitStateDfa::dfa_to_Gdfa(explicit_dfa);
                    
                    ExplicitStateDfaAdd explicit_dfa_add = ExplicitStateDfaAdd::from_dfa_mona(
                        var_mgr_, trimmed_explicit_dfa);
                    SymbolicStateDfa symbolic_dfa = SymbolicStateDfa::from_explicit(
                        std::move(explicit_dfa_add));

                    int color = std::stoi(ltlf_plus_formula_.formula_to_color_.at(ltlf_plus_arg));
                    std::cout << "Converted Forall formula to symbolic DFA for color " << std::endl;
                    color_to_dfa.insert({color, std::move(symbolic_dfa)});
                    break;
                }
                case whitemech::lydia::PrefixQuantifier::Exists: {
                    // Guarantee property: convert to F(phi) form
                    ExplicitStateDfa trimmed_explicit_dfa = ExplicitStateDfa::dfa_to_Fdfa(explicit_dfa);
                    
                    ExplicitStateDfaAdd explicit_dfa_add = ExplicitStateDfaAdd::from_dfa_mona(
                        var_mgr_, trimmed_explicit_dfa);
                    SymbolicStateDfa symbolic_dfa = SymbolicStateDfa::from_explicit(
                        std::move(explicit_dfa_add));
                    std::cout << "Converted Exists formula to symbolic DFA for color "  << std::endl;

                    int color = std::stoi(ltlf_plus_formula_.formula_to_color_.at(ltlf_plus_arg));
                    color_to_dfa.insert({color, std::move(symbolic_dfa)});
                    break;
                }
                default:
                    // This should not happen if validate_obligation_fragment was called
                    throw std::runtime_error("Unexpected quantifier in obligation fragment conversion");
            }
        }

        // Build the product arena following the color formula structure
        SymbolicStateDfa arena = build_arena_from_color_formula(
            ltlf_plus_formula_.color_formula_, color_to_dfa);
        
        // Collect per-color finals (original DFAs) for debugging / downstream use
        for (const auto &p : color_to_dfa) {
            color_to_final_states[p.first] = p.second.final_states();
        }

        // the arena already encodes the combined finals in arena.final_states()
        color_to_final_states[-1] = arena.final_states();
        auto t1 = clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
        std::cout << "[ObligationFragment] DFA created in " << ms << " ms" << std::endl;

        return std::make_pair(arena, color_to_final_states);
    }

    ELSynthesisResult ObligationLTLfPlusSynthesizer::solve_with_scc(
        const SymbolicStateDfa& arena,
        const std::map<int, CUDD::BDD>& color_to_final_states) const {
        
        std::cout << "Solving with BuchiStandalone solver" << std::endl;
        
        // Use the arena's final states which already encode the correct AND/OR structure
        CUDD::BDD accepting_states = arena.final_states();
        
        // Compute state space: all reachable states from initial state (forward reachability)
        auto var_mgr = arena.var_mgr();
        auto mgr = var_mgr->cudd_mgr();
        auto automaton_id = arena.automaton_id();
        auto transition_func = arena.transition_function();
        auto initial_state = arena.initial_state_bdd();
        
        CUDD::BDD state_space = initial_state;
        CUDD::BDD current_layer = initial_state;
        auto transition_vector = var_mgr->make_compose_vector(automaton_id, transition_func);
        CUDD::BDD io_cube = var_mgr->input_cube() * var_mgr->output_cube();
        
        
        std::cout << "State space computed, starting BuchiStandalone" << std::endl;
                std::cout << state_space << std::endl;

        // Create and run the Büchi solver (arena already has final_states)
    BuchiSolver solver(arena, starting_player_, protagonist_player_, var_mgr_->cudd_mgr()->bddOne(), buechi_mode_);
        SynthesisResult game_result = solver.run();
        
        std::cout << "BuchiStandalone completed" << std::endl;
        std::cout << "Realizability: " << (game_result.realizability ? "true" : "false") << std::endl;
        
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
        return solve_with_scc(arena, color_to_final_states);
    }

} // namespace Syft
