//
// Created by Gianmarco Chris on 03/11/25.
//

#include "synthesizer/PPLTLPlusSynthesizer.h"
#include "game/EmersonLei.hpp"
#include <sstream>
#include <cassert>

namespace Syft {
    PPLTLPlusSynthesizer::PPLTLPlusSynthesizer(std::unordered_map<ppltl_plus_ptr, std::string> formula_to_color,
                                             std::unordered_map<ppltl_plus_ptr, PrefixQuantifier> formula_to_quantification,
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

    SynthesisResult PPLTLPlusSynthesizer::run() const {
        std::vector<Syft::SymbolicStateDfa> vec_spec;
        std::vector<CUDD::BDD> goal_states;
        // ensures that the "order" of colors is respected
        std::map<int, SymbolicStateDfa> color_to_dfa;
        std::map<int, CUDD::BDD> color_to_final_states;

        for (const auto& [ppltl_plus_arg, prefix_quantifier] : formula_to_quantification_) {
            ppltl_ptr ppltl_arg = ppltl_plus_arg->ppltl_arg();
            SymbolicStateDfa sdfa = SymbolicStateDfa::dfa_of_ppltl_formula(*ppltl_arg);
            std::cout << "PPLTL formula: " << whitemech::lydia::to_string(*ppltl_arg) << std::endl;

            switch (prefix_quantifier) {
                case PrefixQuantifier::ForallExists:
                    {color_to_dfa.insert({std::stoi(formula_to_color_.at(ppltl_plus_arg)), sdfa});
                    color_to_final_states.insert({std::stoi(formula_to_color_.at(ppltl_plus_arg)), sdfa.final_states()});
                    break;}
                case PrefixQuantifier::ExistsForall:
                    {color_to_dfa.insert({std::stoi(formula_to_color_.at(ppltl_plus_arg)), sdfa});
                    color_to_final_states.insert({std::stoi(formula_to_color_.at(ppltl_plus_arg)), sdfa.final_states()});
                    break;}
                case PrefixQuantifier::Exists:
                    {SymbolicStateDfa edfa = SymbolicStateDfa::get_exists_dfa(sdfa);
                    color_to_dfa.insert({std::stoi(formula_to_color_.at(ppltl_plus_arg)), edfa});
                    color_to_final_states.insert({std::stoi(formula_to_color_.at(ppltl_plus_arg)), edfa.final_states()});
                    break;}
                case PrefixQuantifier::Forall:
                    {SymbolicStateDfa adfa = SymbolicStateDfa::get_exists_dfa(sdfa);
                    color_to_dfa.insert({std::stoi(formula_to_color_.at(ppltl_plus_arg)), adfa});
                    color_to_final_states.insert({std::stoi(formula_to_color_.at(ppltl_plus_arg)), adfa.final_states()});
                    break;}
                default:
                    throw std::runtime_error("Invalid argument in map PPLTL+ formula to prefix quantification");
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
        return solver.run();
    }
}