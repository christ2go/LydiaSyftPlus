//
// Created by Gianmarco Chris on 03/11/25.
//

#include "synthesizer/PPLTLPlusSynthesizer.h"
#include "game/EmersonLei.hpp"
#include "lydia/logic/ppltlplus/base.hpp"
#include "lydia/logic/pp_pnf.hpp"
#include "lydia/parser/ppltlplus/driver.hpp"
#include "lydia/utils/print.hpp"

namespace Syft {
    PPLTLPlusSynthesizer::PPLTLPlusSynthesizer(
        PPLTLPlus ppltl_plus_formula,
        InputOutputPartition partition,
        Player starting_player,
        Player protagonist_player) : 
            ppltl_plus_formula_(ppltl_plus_formula), 
            color_formula_(ppltl_plus_formula.color_formula_), 
            starting_player_(starting_player), 
            protagonist_player_(protagonist_player) {
        std::shared_ptr<Syft::VarMgr> var_mgr = std::make_shared<Syft::VarMgr>();
        var_mgr->create_named_variables(partition.input_variables);
        var_mgr->create_named_variables(partition.output_variables);

        var_mgr->partition_variables(partition.input_variables,
                                     partition.output_variables);
        var_mgr_ = var_mgr;
    }

    ELSynthesisResult PPLTLPlusSynthesizer::run() const {
        std::vector<Syft::SymbolicStateDfa> vec_spec;
        std::vector<CUDD::BDD> goal_states;
        // ensures that the "order" of colors is respected
        std::map<int, SymbolicStateDfa> color_to_dfa;
        std::map<int, CUDD::BDD> color_to_final_states;

        for (const auto& [ppltl_plus_arg, prefix_quantifier] : ppltl_plus_formula_.formula_to_quantification_) {
            whitemech::lydia::ppltl_ptr ppltl_arg = ppltl_plus_arg->ppltl_arg();
            SymbolicStateDfa sdfa = SymbolicStateDfa::dfa_of_ppltl_formula(*ppltl_arg, var_mgr_);
            std::cout << "PPLTL formula: " << whitemech::lydia::to_string(*ppltl_arg) << std::endl;

            switch (prefix_quantifier) {
                case whitemech::lydia::PrefixQuantifier::ForallExists:
                    {color_to_dfa.insert({std::stoi(ppltl_plus_formula_.formula_to_color_.at(ppltl_plus_arg)), sdfa});
                    color_to_final_states.insert({
                      std::stoi(ppltl_plus_formula_.formula_to_color_.at(ppltl_plus_arg)), sdfa.final_states()
                    });
                    break;}
                case whitemech::lydia::PrefixQuantifier::ExistsForall:
                    {color_to_dfa.insert({std::stoi(ppltl_plus_formula_.formula_to_color_.at(ppltl_plus_arg)), sdfa});
                    color_to_final_states.insert({
                      std::stoi(ppltl_plus_formula_.formula_to_color_.at(ppltl_plus_arg)), sdfa.final_states()
                    });
                    break;}
                case whitemech::lydia::PrefixQuantifier::Exists:
                    {SymbolicStateDfa edfa = SymbolicStateDfa::get_exists_dfa(sdfa);
                    color_to_dfa.insert({std::stoi(ppltl_plus_formula_.formula_to_color_.at(ppltl_plus_arg)), edfa});
                    color_to_final_states.insert({
                      std::stoi(ppltl_plus_formula_.formula_to_color_.at(ppltl_plus_arg)), edfa.final_states()
                    });
                    break;}
                case whitemech::lydia::PrefixQuantifier::Forall:
                    {SymbolicStateDfa adfa = SymbolicStateDfa::get_exists_dfa(sdfa);
                    color_to_dfa.insert({std::stoi(ppltl_plus_formula_.formula_to_color_.at(ppltl_plus_arg)), adfa});
                    color_to_final_states.insert({
                      std::stoi(ppltl_plus_formula_.formula_to_color_.at(ppltl_plus_arg)), adfa.final_states()
                    });
                    break;}
                default:
                    throw std::runtime_error("Invalid argument in map PPLTL+ formula to prefix quantification");
            }
        }

        var_mgr_->print_mgr();

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
        std::shared_ptr<EmersonLei> emerson_lei = std::make_shared<EmersonLei>(arena, color_formula_, starting_player_, protagonist_player_,
            goal_states, var_mgr_->cudd_mgr()->bddOne(), var_mgr_->cudd_mgr()->bddZero(), var_mgr_->cudd_mgr()->bddZero());
        emerson_lei_ = emerson_lei;
        return emerson_lei_->run_EL();
    }
}