//
// Created by shuzhu on 04/12/24.
//

#include "synthesizer/LTLfPlusSynthesizer.h"
#include "game/EmersonLei.hpp"
#include "lydia/logic/ltlfplus/base.hpp"
#include "lydia/logic/pnf.hpp"
#include "lydia/parser/ltlfplus/driver.hpp"
#include "lydia/utils/print.hpp"

namespace Syft {
  LTLfPlusSynthesizer::LTLfPlusSynthesizer(LTLfPlus ltlf_plus_formula,
                                           InputOutputPartition partition, Player starting_player,
                                           Player protagonist_player)
    : ltlf_plus_formula_(ltlf_plus_formula),
      color_formula_(ltlf_plus_formula.color_formula_), starting_player_(starting_player),
      protagonist_player_(protagonist_player) {
    std::shared_ptr<VarMgr> var_mgr = std::make_shared<VarMgr>();
    var_mgr->create_named_variables(partition.input_variables);
    var_mgr->create_named_variables(partition.output_variables);

    var_mgr->partition_variables(partition.input_variables,
                                 partition.output_variables);
    var_mgr_ = var_mgr;
  }

  // TODO create a run
  ELSynthesisResult LTLfPlusSynthesizer::run() const {
    std::vector<SymbolicStateDfa> vec_spec;
    std::vector<CUDD::BDD> goal_states;
    // ensures that the "order" of colors is respected
    std::map<int, SymbolicStateDfa> color_to_dfa;
    std::map<int, CUDD::BDD> color_to_final_states;

    for (const auto &[ltlf_plus_arg, prefix_quantifier]: ltlf_plus_formula_.formula_to_quantification_) {
      whitemech::lydia::ltlf_ptr ltlf_arg = ltlf_plus_arg->ltlf_arg();
      ExplicitStateDfa explicit_dfa = ExplicitStateDfa::dfa_of_formula(*ltlf_arg);

      // std::cout << "LTLf formula: " << whitemech::lydia::to_string(*ltlf_arg) << std::endl;
      // std::cout << "------ original DFA: \n";
      // explicit_dfa.dfa_print();

      switch (prefix_quantifier) {
        case whitemech::lydia::PrefixQuantifier::ForallExists: {
          ExplicitStateDfaAdd explicit_dfa_add = ExplicitStateDfaAdd::from_dfa_mona(var_mgr_,
            explicit_dfa);
          SymbolicStateDfa symbolic_dfa = SymbolicStateDfa::from_explicit(
            std::move(explicit_dfa_add));

          color_to_dfa.insert({std::stoi(ltlf_plus_formula_.formula_to_color_.at(ltlf_plus_arg)), symbolic_dfa});
          color_to_final_states.insert({
            std::stoi(ltlf_plus_formula_.formula_to_color_.at(ltlf_plus_arg)), symbolic_dfa.final_states()
          });
          // vec_spec.push_back(symbolic_dfa);
          // goal_states.push_back(symbolic_dfa.final_states());
          break;
        }
        case whitemech::lydia::PrefixQuantifier::ExistsForall: {
          ExplicitStateDfaAdd explicit_dfa_add = ExplicitStateDfaAdd::from_dfa_mona(var_mgr_,
            explicit_dfa);
          SymbolicStateDfa symbolic_dfa = SymbolicStateDfa::from_explicit(
            std::move(explicit_dfa_add));

          color_to_dfa.insert({std::stoi(ltlf_plus_formula_.formula_to_color_.at(ltlf_plus_arg)), symbolic_dfa});
          color_to_final_states.insert({
            std::stoi(ltlf_plus_formula_.formula_to_color_.at(ltlf_plus_arg)), !symbolic_dfa.final_states()
          });
          // vec_spec.push_back(symbolic_dfa);
          // goal_states.push_back(!symbolic_dfa.final_states());
          break;
        }
        case whitemech::lydia::PrefixQuantifier::Forall: {
          ExplicitStateDfa trimmed_explicit_dfa = ExplicitStateDfa::dfa_to_Gdfa(explicit_dfa);
          // new MP: add a new line of dfa_remove_initial_self_loops

          // std::cout << "------ trimmed DFA Gphi: \n";
          // trimmed_explicit_dfa.dfa_print();

          ExplicitStateDfaAdd explicit_dfa_add = ExplicitStateDfaAdd::from_dfa_mona(var_mgr_,
            trimmed_explicit_dfa);
          SymbolicStateDfa symbolic_dfa = SymbolicStateDfa::from_explicit(
            std::move(explicit_dfa_add));

          color_to_dfa.insert({std::stoi(ltlf_plus_formula_.formula_to_color_.at(ltlf_plus_arg)), symbolic_dfa});
          color_to_final_states.insert({
            std::stoi(ltlf_plus_formula_.formula_to_color_.at(ltlf_plus_arg)), symbolic_dfa.final_states()
          });
          // vec_spec.push_back(symbolic_dfa);
          // goal_states.push_back(symbolic_dfa.final_states());
          break;
        }
        case whitemech::lydia::PrefixQuantifier::Exists: {
          std::vector<size_t> final_states = explicit_dfa.get_final();

          // new MP: as it is  
          ExplicitStateDfa trimmed_explicit_dfa = ExplicitStateDfa::dfa_to_Fdfa(explicit_dfa);
          // std::cout << "------ trimmed DFA Fphi: \n";
          // trimmed_explicit_dfa.dfa_print();

          ExplicitStateDfaAdd explicit_dfa_add = ExplicitStateDfaAdd::from_dfa_mona(var_mgr_,
            trimmed_explicit_dfa);
          SymbolicStateDfa symbolic_dfa = SymbolicStateDfa::from_explicit(
            std::move(explicit_dfa_add));

          color_to_dfa.insert({std::stoi(ltlf_plus_formula_.formula_to_color_.at(ltlf_plus_arg)), symbolic_dfa});
          color_to_final_states.insert({
            std::stoi(ltlf_plus_formula_.formula_to_color_.at(ltlf_plus_arg)), symbolic_dfa.final_states()
          });
          // vec_spec.push_back(symbolic_dfa);
          // goal_states.push_back(symbolic_dfa.final_states());
          break;
        }
        default:
          throw std::runtime_error("Invalid argument in map LTLf+ formula to prefix quantification");
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

    // for (auto j = 0; j < vec_spec.size(); j++) {
    //   vec_spec[j].dump_dot("dfa" + std::to_string(j) + ".dot");
    // }

    SymbolicStateDfa arena = SymbolicStateDfa::product_AND(vec_spec);
    // arena.dump_dot("arena.dot");
    std::shared_ptr<EmersonLei> emerson_lei = std::make_shared<EmersonLei>(arena, color_formula_, starting_player_, protagonist_player_,
                      goal_states, var_mgr_->cudd_mgr()->bddOne(), var_mgr_->cudd_mgr()->bddZero(), var_mgr_->cudd_mgr()->bddZero(), false);
    emerson_lei_ = emerson_lei;
    return emerson_lei_->run_EL();
  }

  // EmersonLei::OneStepSynReturn LTLfPlusSynthesizer::synthesize(std::string X, ELSynthesisResult result) const {
  //   return emerson_lei_->synthesize(X, result);
  // }

}
