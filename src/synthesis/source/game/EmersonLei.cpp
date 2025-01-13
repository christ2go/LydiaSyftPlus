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
        std::cout << "Colors: \n";
        for (size_t i = 0; i < Colors_.size(); i++){
            std::cout << Colors_[i] << '\n';
        }

		// build Zielonka tree; parse formula from PHI_FILE, number of colors taken from Colors
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
			// result.transducer = AbstractSingleStrategy(Ztree->get_root(),result);
			return result;
		} else {
			result.realizability = false;
			result.winning_states = winning_states;
			result.winning_moves = var_mgr_->cudd_mgr()->bddZero();
			result.transducer = nullptr;
			return result;
		}
	}

CUDD::BDD EmersonLei::ExtractStrategy(SynthesisResult* result, ZielonkaNode *t) const {
//		v: game node, t: tree node, s (anchor node): lowest ancester of t that includes all colors of v
	ZielonkaNode *s = get_anchor(v, t)
//      // now get Y
		int index = 0;
//      while (index < s.children_size()) {
			if (v in s.winningmoves[index]) {
				Y = s.winningmove(v);
				break;
			}
			index++;
		}
		// now get t'
		// box is winning
//	    if s->winning:
			int branch = the index that leads to t
			next_branch = (branch + 1 ) mod size(s->children) // next branch to explore

//
//		    (index of branch from s to t +1) mod size(s->children).
//	    if s->losing:
//		    strategy(v,t) = pick one of (s->winningmoves(v)), (index of branch with picked move).
//	}


	ZielonkaNode* EmersonLei::get_next_t(CUDD::BDD state, ZielonkaNode *t, ZielonkaNode *anchor_node, ZielonkaNode *curr, CUDD::BDD Y) const {
		if (curr == anchor_node) {
			if (curr->winning) {
				//TODO

				next_branch = (branch + 1 ) mod size(s->children) // next branch to explore
				get_next_t(state, t, anchor_node, t->children{next_branch}, Y);
			}

//
//		    (index of branch from s to t +1) mod size(s->children).
//	    if s->losing:
//		    strategy(v,t) = pick one of (s->winningmoves(v)), (index of branch with picked move).
		}
	}



	CUDD::BDD EmersonLei::cpre(ZielonkaNode *t, int i, CUDD::BDD target) const {

	    CUDD::BDD result;

	    if (starting_player_ == Player::Agent) {
                CUDD::BDD quantified_X_transitions_to_winning_states = preimage(target);
    //             CUDD::BDD new_target_moves = target |
    //                                 (state_space_ & (!target) & quantified_X_transitions_to_winning_states);
    //             result = project_into_states(new_target_moves);
				// CUDD::BDD diffmoves = (result & (!target) & quantified_X_transitions_to_winning_states);
				if (t->winning) {
					CUDD::BDD new_target_moves =  (state_space_ & quantified_X_transitions_to_winning_states);
					result = project_into_states(new_target_moves);
					// CUDD::BDD diffmoves = (result & (!target) & quantified_X_transitions_to_winning_states);
            		t->winningmoves[i] = t->winningmoves[i] & new_target_moves;
        		} else {
        			CUDD::BDD new_target_moves =
									(state_space_ & (!target) & quantified_X_transitions_to_winning_states);
        			result = project_into_states(new_target_moves);
        			// CUDD::BDD diffmoves = (result & (!target) & quantified_X_transitions_to_winning_states);
            		t->winningmoves[i] = t->winningmoves[i] | new_target_moves;
        		}
            } else {
                CUDD::BDD transitions_to_target_states = preimage(target);
				if (t->winning) {
					CUDD::BDD result = state_space_ & project_into_states(transitions_to_target_states);
					// result = target | new_collected_target_states;
					CUDD::BDD new_target_moves =  result & transitions_to_target_states;
					// CUDD::BDD diffmoves = (!target) & transitions_to_target_states;
            		t->winningmoves[i] = t->winningmoves[i] & new_target_moves;
        		} else {
        			CUDD::BDD result = state_space_ & project_into_states(transitions_to_target_states);
        			// result = target | new_collected_target_states;
        			CUDD::BDD new_target_moves = (!target) & result & transitions_to_target_states;
            		t->winningmoves[i] = t->winningmoves[i] | new_target_moves;
        		}

            }
	    std::cout << "cpre: " << result << "\n";
	    return result;
	}

	CUDD::BDD EmersonLei::EmersonLeiSolve(ZielonkaNode *t, CUDD::BDD term) const {
       	CUDD::BDD X, XX;

	    // initialize variables for fixpoint computation (gfp for winning / lfp for losing)
        if (t->winning) {
            X = var_mgr_->cudd_mgr()->bddOne();
            if (t->children.empty()) {
            	t->winningmoves[0]=var_mgr_->cudd_mgr()->bddOne();
	            // t->winningmoves.push_back(var_mgr_->cudd_mgr()->bddOne());
            }
            else {
				for (int i = 0; i < t->children.size(); i++) {
                	t->winningmoves[i] = var_mgr_->cudd_mgr()->bddOne();
				}
			}
  		} else {
            X = var_mgr_->cudd_mgr()->bddZero();
            if (t->children.empty()) { 
	            t->winningmoves[0]=var_mgr_->cudd_mgr()->bddZero();
            }
            else {
				for (int i = 0; i < t->children.size(); i++) {
                	t->winningmoves[i] = var_mgr_->cudd_mgr()->bddZero();
				}
			}
        }

	    // loop until fixpoint has stabilized
      	while (true) {
            std::cout << "Node: " << t->order << "\n";
            std::cout << X << "\n";

			// if t is a leaf
            if (t->children.empty()) { 
                XX = term | (t->safenodes & cpre(t, 0, X));
            }

			// if t is not a leaf
            else {

		   	// initialize intersecion for winning / union for losing
                if (t->winning) {
                    XX = var_mgr_->cudd_mgr()->bddOne(); }
                else {
                    XX = var_mgr_->cudd_mgr()->bddZero(); }

		   	// iterate over direct children of t
                // for (auto s : t->children) {
                for (int i = 0; i < t->children.size(); i++) {
				// add new choice to term
                	auto s = t->children[i];
                    CUDD::BDD current_term = term | (s->targetnodes & cpre(t, i, X));
                    if (t->winning) {
					    // intersect with recursively computed solution for s and current term
                        XX &= EmersonLeiSolve(s, current_term);
                    } else {
			   			// union with recursively computed solution for s and current term
                        XX |= EmersonLeiSolve(s, current_term);
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