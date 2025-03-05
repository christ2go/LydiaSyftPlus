//
// Created by Shufang Zhu on 05/03/2025.
//

#include "synthesizer/LTLfPlusSynthesizerMP.h"
#include "game/MannaPnueli.hpp"

namespace Syft {
  LTLfPlusSynthesizerMP::LTLfPlusSynthesizerMP(LTLfPlus ltlf_plus_formula,
                                               InputOutputPartition partition, Player starting_player,
                                               Player protagonist_player)
    : ltlf_plus_formula_(ltlf_plus_formula), starting_player_(starting_player),
      protagonist_player_(protagonist_player) {
    std::shared_ptr<VarMgr> var_mgr = std::make_shared<VarMgr>();
    var_mgr->create_named_variables(partition.input_variables);
    var_mgr->create_named_variables(partition.output_variables);

    var_mgr->partition_variables(partition.input_variables,
                                 partition.output_variables);
    var_mgr_ = var_mgr;
    for (const auto &[ltlf_plus_arg, prefix_quantifier]: ltlf_plus_formula_.formula_to_quantification_) {
      switch (prefix_quantifier) {
        case whitemech::lydia::PrefixQuantifier::ForallExists: {
          break;
        }
        case whitemech::lydia::PrefixQuantifier::ExistsForall: {
          break;
        }
        case whitemech::lydia::PrefixQuantifier::Forall: {
          int color = std::stoi(ltlf_plus_formula_.formula_to_color_[ltlf_plus_arg]);
          if (std::find(G_colors_.begin(), G_colors_.end(), color) == G_colors_.end()) {
            G_colors_.push_back(color);
          }
          break;
        }
        case whitemech::lydia::PrefixQuantifier::Exists: {
          int color = std::stoi(ltlf_plus_formula_.formula_to_color_[ltlf_plus_arg]);
          if (std::find(F_colors_.begin(), F_colors_.end(), color) == F_colors_.end()) {
            F_colors_.push_back(color);
          }
          break;
        }
        default:
          throw std::runtime_error("Invalid argument in map LTLf+ formula to prefix quantification");
      }
    }
  }


  MPSynthesisResult LTLfPlusSynthesizerMP::run() const {
    std::vector<SymbolicStateDfa> vec_spec;
    std::vector<CUDD::BDD> goal_states;
    // ensures that the "order" of colors is respected
    std::map<int, SymbolicStateDfa> color_to_dfa;
    std::map<int, CUDD::BDD> color_to_final_states;

    for (const auto &[ltlf_plus_arg, prefix_quantifier]: ltlf_plus_formula_.formula_to_quantification_) {
      whitemech::lydia::ltlf_ptr ltlf_arg = ltlf_plus_arg->ltlf_arg();
      ExplicitStateDfa explicit_dfa = ExplicitStateDfa::dfa_of_formula(*ltlf_arg);

      std::cout << "LTLf formula: " << whitemech::lydia::to_string(*ltlf_arg) << std::endl;
      std::cout << "------ original DFA: \n";
      explicit_dfa.dfa_print();

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
        case whitemech::lydia::PrefixQuantifier::Exists: {
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

    for (auto j = 0; j < vec_spec.size(); j++) {
      vec_spec[j].dump_dot("dfa" + std::to_string(j) + ".dot");
    }

    SymbolicStateDfa arena = SymbolicStateDfa::product_AND(vec_spec);
    arena.dump_dot("arena.dot");
    MannaPnueli solver(arena, ltlf_plus_formula_.color_formula_, F_colors_, G_colors_, starting_player_,
                       protagonist_player_,
                       goal_states, var_mgr_->cudd_mgr()->bddOne());
    return solver.run_MP();
  }
}
