//
// Created by shuzhu on 04/12/24.
//

#include "synthesizer/LTLfPlusSynthesizer.h"
#include "lydia/parser/ltlf/driver.hpp"
#include "game/EmersonLei.hpp"
#include <sstream>
#include <cassert>

namespace Syft {
    LTLfPlusSynthesizer::LTLfPlusSynthesizer(std::map<char, Syft::LTLfPlus> &ltlfplus_spec,
                                             const std::string &color_formula,
                                             const Syft::InputOutputPartition partition, Player starting_player,
                                             Player protagonist_player)
            : ltlfplus_spec_(ltlfplus_spec), color_formula_(color_formula), starting_player_(starting_player), protagonist_player_(protagonist_player) {

        std::shared_ptr<Syft::VarMgr> var_mgr = std::make_shared<Syft::VarMgr>();
        var_mgr->create_named_variables(partition.input_variables);
        var_mgr->create_named_variables(partition.output_variables);

        var_mgr->partition_variables(partition.input_variables,
                                     partition.output_variables);
        var_mgr_ = var_mgr;
    }


    SynthesisResult LTLfPlusSynthesizer::run() const {
        auto driver = std::make_shared<whitemech::lydia::parsers::ltlf::LTLfDriver>();
        std::vector<Syft::SymbolicStateDfa> vec_spec;
        std::vector<CUDD::BDD> goal_states;

        for (auto const& [key, val] : ltlfplus_spec_)
        {
            std::stringstream formula_stream(val.formula_);
            driver->parse(formula_stream);
            whitemech::lydia::ltlf_ptr parsed_formula = driver->get_result();
            // Apply no-empty semantics
            auto context = driver->context;
            auto not_end = context->makeLtlfNotEnd();
            parsed_formula = context->makeLtlfAnd({parsed_formula, not_end});;
            Syft::ExplicitStateDfa explicit_dfa = Syft::ExplicitStateDfa::dfa_of_formula(*parsed_formula);

            std::cout << "------ original DFA: \n";
            explicit_dfa.dfa_print();

            if (val.label_ == LTLfLabel::GF) {
                Syft::ExplicitStateDfaAdd explicit_dfa_add = Syft::ExplicitStateDfaAdd::from_dfa_mona(var_mgr_,
                                                                                                      explicit_dfa);
                Syft::SymbolicStateDfa symbolic_dfa = Syft::SymbolicStateDfa::from_explicit(
                        std::move(explicit_dfa_add));
                vec_spec.push_back(symbolic_dfa);
                goal_states.push_back(symbolic_dfa.final_states());
            } else if (val.label_ == LTLfLabel::FG) {
                Syft::ExplicitStateDfaAdd explicit_dfa_add = Syft::ExplicitStateDfaAdd::from_dfa_mona(var_mgr_,
                                                                                                      explicit_dfa);
                Syft::SymbolicStateDfa symbolic_dfa = Syft::SymbolicStateDfa::from_explicit(
                        std::move(explicit_dfa_add));
                vec_spec.push_back(symbolic_dfa);
                goal_states.push_back(!symbolic_dfa.final_states());
            } else if (val.label_ == LTLfLabel::G) {
                Syft::ExplicitStateDfa trimmed_explicit_dfa = Syft::ExplicitStateDfa::dfa_to_Gdfa(explicit_dfa);

                std::cout << "------ trimmed DFA Gphi: \n";
                trimmed_explicit_dfa.dfa_print();

                Syft::ExplicitStateDfaAdd explicit_dfa_add = Syft::ExplicitStateDfaAdd::from_dfa_mona(var_mgr_,
                                                                                                      trimmed_explicit_dfa);
                Syft::SymbolicStateDfa symbolic_dfa = Syft::SymbolicStateDfa::from_explicit(
                        std::move(explicit_dfa_add));
                vec_spec.push_back(symbolic_dfa);
                goal_states.push_back(symbolic_dfa.final_states());
            } else if (val.label_ == LTLfLabel::F) {
                std::vector<size_t> final_states = explicit_dfa.get_final();

                Syft::ExplicitStateDfa trimmed_explicit_dfa = Syft::ExplicitStateDfa::dfa_to_Fdfa(explicit_dfa);
                std::cout << "------ trimmed DFA Fphi: \n";
                trimmed_explicit_dfa.dfa_print();

                Syft::ExplicitStateDfaAdd explicit_dfa_add = Syft::ExplicitStateDfaAdd::from_dfa_mona(var_mgr_,
                                                                                                      trimmed_explicit_dfa);
                Syft::SymbolicStateDfa symbolic_dfa = Syft::SymbolicStateDfa::from_explicit(
                        std::move(explicit_dfa_add));
                vec_spec.push_back(symbolic_dfa);
                goal_states.push_back(symbolic_dfa.final_states());
            }
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

