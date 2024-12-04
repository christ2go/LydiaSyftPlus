//
// Created by dh on 25/11/24.
//

#include "game/EmersonLei.hpp"

namespace Syft {
	EmersonLei::EmersonLei(const SymbolicStateDfa &spec, const std::string color_formula, Player starting_player,
	Player protagonist_player,
	const std::vector<CUDD::BDD> &colorBDDs,
	const CUDD::BDD &state_space)
	: DfaGameSynthesizer(spec, starting_player, protagonist_player), color_formula_(color_formula), Colors_(colorBDDs),
	state_space_(state_space) {
	}
	
	SynthesisResult EmersonLei::run() const {

		// build Zielonka tree; parse formula from PHI_FILE, number of colors taken from colorBDDs
		ZielonkaTree *Ztree = new ZielonkaTree(color_formula_, Colors_, var_mgr_);

		// solve EL game for root of Zielonka tree and BDD encoding emptyset as set of states currently assumed to be winning
		CUDD::BDD winning_states = EmersonLeiSolve(Ztree->get_root(), var_mgr_->cudd_mgr()->bddZero());

		// update result according to computed solution
		SynthesisResult result;
		if (includes_initial_state(winning_states)) {
			result.realizability = true;
			result.winning_states = winning_states;
			result.winning_moves = var_mgr_->cudd_mgr()->bddZero();
			result.transducer = AbstractSingleStrategy(result);
			return result;
		} else {
			result.realizability = false;
			result.winning_states = winning_states;
			result.winning_moves = var_mgr_->cudd_mgr()->bddZero();
			result.transducer = nullptr;
			return result;
		}
	}

	CUDD::BDD EmersonLei::cpre(CUDD::BDD target) const {

	    CUDD::BDD result;

	    if (starting_player_ == Player::Agent) {
                CUDD::BDD quantified_X_transitions_to_winning_states = preimage(target);
                CUDD::BDD new_target_moves = target |
                                    (state_space_ & (!target) & quantified_X_transitions_to_winning_states);

                result = project_into_states(quantified_X_transitions_to_winning_states);
            } else {
                CUDD::BDD transitions_to_target_states = preimage(target);
                CUDD::BDD new_collected_target_states = project_into_states(transitions_to_target_states);
                result = target | new_collected_target_states;
                CUDD::BDD new_target_moves = target |
                                    ((!target) & new_collected_target_states & transitions_to_target_states);
            }
	
	    return result;
	}

	CUDD::BDD EmersonLei::EmersonLeiSolve(ZielonkaNode *t, CUDD::BDD term) const {
        CUDD::BDD X, XX;

	    // initialize variable for fixpoint computation (lfp/gfp)
            if (t->winning) {
                X = var_mgr_->cudd_mgr()->bddZero();
            } else {
                X = var_mgr_->cudd_mgr()->bddOne();
            }

	    // loop until fixpoint has stabilized
      	    while (true) {

		// if t is a leaf
                if (t->children.empty()) { 
                    XX = term | t->avoidnodes & cpre(X);
                }

		// if t is not a leaf
                else {

		    // initialize intersecion/union
                    if (t->winning)
                        XX = var_mgr_->cudd_mgr()->bddOne();
                    else
                        XX = var_mgr_->cudd_mgr()->bddZero();

		    // iterate over direct children of t
                    for (auto s : t->children) { 

			// add new choice to term
                        term |= s->targetnodes & cpre(X);

                        CUDD::BDD U = EmersonLeiSolve(s, term);

                        if (t->winning) {
			    // intersect with recursively computed solution for s and new term
                            XX &= EmersonLeiSolve(s, term);
                        } else {
			    // union with recursively computed solution for s and new term
                            XX |= EmersonLeiSolve(s, term);
                        }
                    }
                }

		if (X == XX) {
		  break;
		} 
                else { 
 		  X = XX;
                }

		
            }

	    // return stabilized fixpoint
            return X;

	}

	
}