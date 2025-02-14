//
// Created by shuzhu on 04/12/24.
//

#include "synthesizer/LTLfPlusSynthesizer.h"
#include "game/EmersonLei.hpp"
#include <sstream>
#include <cassert>

namespace Syft {
    LTLfPlusSynthesizer::LTLfPlusSynthesizer(std::unordered_map<ltlf_plus_ptr, std::string> formula_to_color,
                                            std::unordered_map<ltlf_plus_ptr, PrefixQuantifier> formula_to_quantification,
                                            const std::string &color_formula,
                                            const Syft::InputOutputPartition partition, Player starting_player,
                                            Player protagonist_player)
            : formula_to_color_(formula_to_color), formula_to_quantification_(formula_to_quantification), color_formula_(color_formula), starting_player_(starting_player), protagonist_player_(protagonist_player) {

        std::shared_ptr<Syft::VarMgr> var_mgr = std::make_shared<Syft::VarMgr>();
        var_mgr->create_named_variables(partition.input_variables);
        var_mgr->create_named_variables(partition.output_variables);

        var_mgr->partition_variables(partition.input_variables,
                                     partition.output_variables);
        var_mgr_ = var_mgr;
    }


    ELSynthesisResult LTLfPlusSynthesizer::run() const {
        std::vector<Syft::SymbolicStateDfa> vec_spec;
        std::vector<CUDD::BDD> goal_states;
        // ensures that the "order" of colors is respected
        std::map<int, SymbolicStateDfa> color_to_dfa;
        std::map<int, CUDD::BDD> color_to_final_states;

        for (const auto& [ltlf_plus_arg, prefix_quantifier] : formula_to_quantification_) {
            ltlf_ptr ltlf_arg = ltlf_plus_arg->ltlf_arg();
            Syft::ExplicitStateDfa explicit_dfa = Syft::ExplicitStateDfa::dfa_of_formula(*ltlf_arg);

            std::cout << "LTLf formula: " << whitemech::lydia::to_string(*ltlf_arg) << std::endl;
            std::cout << "------ original DFA: \n";
            explicit_dfa.dfa_print();

            switch (prefix_quantifier) {
                case PrefixQuantifier::ForallExists: {
                    Syft::ExplicitStateDfaAdd explicit_dfa_add = Syft::ExplicitStateDfaAdd::from_dfa_mona(var_mgr_,
                                                                                                      explicit_dfa);
                    Syft::SymbolicStateDfa symbolic_dfa = Syft::SymbolicStateDfa::from_explicit(
                            std::move(explicit_dfa_add));
                    
                    color_to_dfa.insert({std::stoi(formula_to_color_.at(ltlf_plus_arg)), symbolic_dfa});
                    color_to_final_states.insert({std::stoi(formula_to_color_.at(ltlf_plus_arg)), symbolic_dfa.final_states()});
                    // vec_spec.push_back(symbolic_dfa);
                    // goal_states.push_back(symbolic_dfa.final_states());    
                    break;
                    }
                case PrefixQuantifier::ExistsForall: {
                    Syft::ExplicitStateDfaAdd explicit_dfa_add = Syft::ExplicitStateDfaAdd::from_dfa_mona(var_mgr_,
                                                                                                      explicit_dfa);
                    Syft::SymbolicStateDfa symbolic_dfa = Syft::SymbolicStateDfa::from_explicit(
                        std::move(explicit_dfa_add));

                    color_to_dfa.insert({std::stoi(formula_to_color_.at(ltlf_plus_arg)), symbolic_dfa});
                    color_to_final_states.insert({std::stoi(formula_to_color_.at(ltlf_plus_arg)), !symbolic_dfa.final_states()});
                    // vec_spec.push_back(symbolic_dfa);
                    // goal_states.push_back(!symbolic_dfa.final_states());
                    break;
                    }
                case PrefixQuantifier::Forall: {
                    Syft::ExplicitStateDfa trimmed_explicit_dfa = Syft::ExplicitStateDfa::dfa_to_Gdfa(explicit_dfa);

                    std::cout << "------ trimmed DFA Gphi: \n";
                    trimmed_explicit_dfa.dfa_print();

                    Syft::ExplicitStateDfaAdd explicit_dfa_add = Syft::ExplicitStateDfaAdd::from_dfa_mona(var_mgr_,
                                                                                                      trimmed_explicit_dfa);
                    Syft::SymbolicStateDfa symbolic_dfa = Syft::SymbolicStateDfa::from_explicit(
                        std::move(explicit_dfa_add));

                    color_to_dfa.insert({std::stoi(formula_to_color_.at(ltlf_plus_arg)), symbolic_dfa});
                    color_to_final_states.insert({std::stoi(formula_to_color_.at(ltlf_plus_arg)), symbolic_dfa.final_states()});
                    // vec_spec.push_back(symbolic_dfa);
                    // goal_states.push_back(symbolic_dfa.final_states());
                    break;
                    }
                case PrefixQuantifier::Exists: {
                    std::vector<size_t> final_states = explicit_dfa.get_final();

                    Syft::ExplicitStateDfa trimmed_explicit_dfa = Syft::ExplicitStateDfa::dfa_to_Fdfa(explicit_dfa);
                    std::cout << "------ trimmed DFA Fphi: \n";
                    trimmed_explicit_dfa.dfa_print();

                    Syft::ExplicitStateDfaAdd explicit_dfa_add = Syft::ExplicitStateDfaAdd::from_dfa_mona(var_mgr_,
                                                                                                      trimmed_explicit_dfa);
                    Syft::SymbolicStateDfa symbolic_dfa = Syft::SymbolicStateDfa::from_explicit(
                        std::move(explicit_dfa_add));

                    color_to_dfa.insert({std::stoi(formula_to_color_.at(ltlf_plus_arg)), symbolic_dfa});
                    color_to_final_states.insert({std::stoi(formula_to_color_.at(ltlf_plus_arg)), symbolic_dfa.final_states()});
                    // vec_spec.push_back(symbolic_dfa);
                    // goal_states.push_back(symbolic_dfa.final_states());
                    break;
                    }
                default:
                    throw std::runtime_error("Invalid argument in map LTLf+ formula to prefix quantification");
            }
        }

        for (const auto& [color, dfa] : color_to_dfa) {
            vec_spec.push_back(color_to_dfa.at(color));
            goal_states.push_back(color_to_final_states.at(color));
        }

        std::size_t n_colors = goal_states.size();
        for (auto i = 0; i < n_colors; i++){
            goal_states.push_back(!goal_states[i]);
        }

        for (auto j = 0; j < vec_spec.size(); j++) {
            vec_spec[j].dump_dot("dfa"+std::to_string(j)+".dot");
        }

        SymbolicStateDfa arena = SymbolicStateDfa::product_AND(vec_spec);
        arena.dump_dot("arena.dot");
        EmersonLei solver(arena, color_formula_, starting_player_, protagonist_player_,
        goal_states, var_mgr_->cudd_mgr()->bddOne());
        return solver.run_EL();
    }
}

