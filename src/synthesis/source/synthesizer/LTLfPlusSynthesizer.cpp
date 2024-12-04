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


            Syft::ExplicitStateDfaAdd explicit_dfa_add = Syft::ExplicitStateDfaAdd::from_dfa_mona(var_mgr_,
                                                                                                  explicit_dfa);

            Syft::SymbolicStateDfa symbolic_dfa = Syft::SymbolicStateDfa::from_explicit(
                    std::move(explicit_dfa_add));
            vec_spec.push_back(symbolic_dfa);
            if (val.label_ == LTLfLabel::GF) {
                goal_states.push_back(symbolic_dfa.final_states());
            }
        }

        SymbolicStateDfa arena = SymbolicStateDfa::product_AND(vec_spec);
        EmersonLei solver(arena, color_formula_, starting_player_, protagonist_player_,
        goal_states, var_mgr_->cudd_mgr()->bddOne());
        return solver.run();

    }

}

