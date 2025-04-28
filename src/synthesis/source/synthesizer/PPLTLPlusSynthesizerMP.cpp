//
// Created by Gianmarco&Chris on 04/01/2025
// 

#include "synthesizer/PPLTLPlusSynthesizerMP.h"
#include "game/MannaPnueli.hpp"

namespace Syft {
    PPLTLPlusSynthesizerMP::PPLTLPlusSynthesizerMP(
        PPLTLPlus ppltl_plus_formula,
        InputOutputPartition partition,
        Player starting_player,
        Player protagonist_player
    ) : ppltl_plus_formula_(ppltl_plus_formula), starting_player_(starting_player),
        protagonist_player_(protagonist_player) {
        std::shared_ptr<VarMgr> var_mgr = std::make_shared<VarMgr>();
        var_mgr->create_named_variables(partition.input_variables);
        var_mgr->create_named_variables(partition.output_variables);
        var_mgr->partition_variables(partition.input_variables, partition.output_variables);
        var_mgr_ = var_mgr;

        for (const auto& [ppltl_plus_arg, prefix_quantifier] : ppltl_plus_formula_.formula_to_quantification_) {
            switch (prefix_quantifier) {
                case whitemech::lydia::PrefixQuantifier::ForallExists: {
                    break;
                }
                  case whitemech::lydia::PrefixQuantifier::ExistsForall: {
                    break;
                }
                case whitemech::lydia::PrefixQuantifier::Forall: {
                    int color = std::stoi(ppltl_plus_formula_.formula_to_color_[ppltl_plus_arg]);
                    if (std::find(G_colors_.begin(), G_colors_.end(), color) == G_colors_.end()) {
                        G_colors_.push_back(color);
                    }
                    break;
                }
                case whitemech::lydia::PrefixQuantifier::Exists: {
                    int color = std::stoi(ppltl_plus_formula_.formula_to_color_[ppltl_plus_arg]);
                    if (std::find(F_colors_.begin(), F_colors_.end(), color) == F_colors_.end()) {
                        F_colors_.push_back(color);
                    }
                    break;
                }
                default:
                    throw std::runtime_error("Invalid argument in map PPLTL+ formula to prefix quantification");
            }
        }
    }

    MPSynthesisResult PPLTLPlusSynthesizerMP::run() const {
        std::vector<SymbolicStateDfa> vec_spec;
        std::vector<CUDD::BDD> goal_states;
        // ensures that the "order" of colors is respected
        std::map<int, SymbolicStateDfa> color_to_dfa;
        std::map<int, CUDD::BDD> color_to_final_states;

        for (const auto& [ppltl_plus_arg, prefix_quantifier] : ppltl_plus_formula_.formula_to_quantification_) {
            whitemech::lydia::ppltl_ptr ppltl_arg = ppltl_plus_arg -> ppltl_arg();
            std::cout << "PPLTL formula: " << whitemech::lydia::to_string(*ppltl_arg) << std::endl;

            switch (prefix_quantifier) {
                case whitemech::lydia::PrefixQuantifier::ForallExists:
                    {
                    SymbolicStateDfa sdfa = SymbolicStateDfa::dfa_of_ppltl_formula(*ppltl_arg, var_mgr_);    
                    color_to_dfa.insert({std::stoi(ppltl_plus_formula_.formula_to_color_.at(ppltl_plus_arg)), sdfa});
                    color_to_final_states.insert({
                      std::stoi(ppltl_plus_formula_.formula_to_color_.at(ppltl_plus_arg)), sdfa.final_states()
                    });
                    break;}
                case whitemech::lydia::PrefixQuantifier::ExistsForall: 
                    {
                    SymbolicStateDfa sdfa = SymbolicStateDfa::dfa_of_ppltl_formula(*ppltl_arg, var_mgr_); 
                    color_to_dfa.insert({std::stoi(ppltl_plus_formula_.formula_to_color_.at(ppltl_plus_arg)), sdfa});
                    color_to_final_states.insert({
                      std::stoi(ppltl_plus_formula_.formula_to_color_.at(ppltl_plus_arg)), !sdfa.final_states()
                    });
                    break;}
                case whitemech::lydia::PrefixQuantifier::Forall: {
                    SymbolicStateDfa sdfa = SymbolicStateDfa::dfa_of_ppltl_formula_remove_initial_self_loops(*ppltl_arg, var_mgr_); 
                    color_to_dfa.insert({std::stoi(ppltl_plus_formula_.formula_to_color_.at(ppltl_plus_arg)), sdfa});
                    // CUDD::BDD final_states = sdfa.final_states() + sdfa.initial_state_bdd();
                    color_to_final_states.insert(
                        {std::stoi(ppltl_plus_formula_.formula_to_color_.at(ppltl_plus_arg)), sdfa.final_states()
                    });
                    break;                    
                }
                case whitemech::lydia::PrefixQuantifier::Exists: 
                {
                    SymbolicStateDfa sdfa = SymbolicStateDfa::dfa_of_ppltl_formula(*ppltl_arg, var_mgr_); 
                    color_to_dfa.insert({std::stoi(ppltl_plus_formula_.formula_to_color_.at(ppltl_plus_arg)), sdfa});
                    color_to_final_states.insert(
                        {std::stoi(ppltl_plus_formula_.formula_to_color_.at(ppltl_plus_arg)), sdfa.final_states()
                    });
                    break;
                }
                default:
                    throw std::runtime_error("Invalid argument in map PPLTL+ formula to prefix quantification");
            }
        }
        for (const auto &[color, dfa]: color_to_dfa) {
              vec_spec.push_back(color_to_dfa.at(color));
              goal_states.push_back(color_to_final_states.at(color));
            }
        
            std::size_t n_colors = goal_states.size();
            for (auto i = 0; i < n_colors; i++) {
              goal_states.push_back(!goal_states[i]);
            }
        
            for (auto j = 0; j < vec_spec.size(); j++) {
              vec_spec[j].dump_dot("dfa" + std::to_string(j) + ".dot");
            }
        
            SymbolicStateDfa arena = SymbolicStateDfa::product_AND(vec_spec);
            arena.dump_dot("arena.dot");
            MannaPnueli solver(arena, ppltl_plus_formula_.color_formula_, F_colors_, G_colors_, starting_player_,
                               protagonist_player_,
                               goal_states, var_mgr_->cudd_mgr()->bddOne());
            return solver.run_MP();
    }
}