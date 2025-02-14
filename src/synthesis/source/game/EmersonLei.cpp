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
		SynthesisResult result;
		result.realizability = true;
		result.winning_states = var_mgr_->cudd_mgr()->bddZero();
		result.winning_moves = var_mgr_->cudd_mgr()->bddZero();
		result.transducer = nullptr;
		return result;
	}

	ELSynthesisResult EmersonLei::run_EL() const {
        std::cout << "Colors: \n";
        for (size_t i = 0; i < Colors_.size(); i++){
            std::cout << Colors_[i] << '\n';
        }

		// build Zielonka tree; parse formula from PHI_FILE, number of colors taken from Colors
		ZielonkaTree *Ztree = new ZielonkaTree(color_formula_, Colors_, var_mgr_);


		// solve EL game for root of Zielonka tree and BDD encoding emptyset as set of states currently assumed to be winning
		CUDD::BDD winning_states = EmersonLeiSolve(Ztree->get_root(), var_mgr_->cudd_mgr()->bddZero());

		// update result according to computed solution
		ELSynthesisResult result;
		if (includes_initial_state(winning_states)) {
			result.realizability = true;
			result.winning_states = winning_states;
			EL_output_function op;
			result.output_function = ExtractStrategy_Explicit(op, spec_.initial_state_bdd(), Ztree->get_root());
			return result;
		} else {
			result.realizability = false;
			result.winning_states = winning_states;
			EL_output_function output_function;
			result.output_function = output_function;
			return result;
		}
	}


	int EmersonLei::index_below(ZielonkaNode *anchor_node, ZielonkaNode *old_memory) const {
		if (old_memory == anchor_node) {
			return 0;
		}
		else {
			if ((old_memory->parent) == anchor_node) {
				for (int i = 0; i < (old_memory->parent)->children.size(); i++) {
					if (old_memory == (old_memory->parent)->children[i]) {
						return i;
					}
				}
			} else {
					return index_below(anchor_node, old_memory->parent);
			}
		}

	}

	ZielonkaNode* EmersonLei::get_anchor(CUDD::BDD game_node, ZielonkaNode *memory_value) const {
		CUDD::BDD restricted = game_node * memory_value->targetnodes;
		if (restricted != var_mgr_->cudd_mgr()->bddZero()) {
			return memory_value;
		}
		else {
			return (get_anchor(game_node, memory_value->parent));
		}
	}

	ZielonkaNode* EmersonLei::get_leaf(ZielonkaNode *old_memory, ZielonkaNode *anchor_node, ZielonkaNode *curr, CUDD::BDD Y) const {

		// case: a leaf has been reached
		if (curr->children.empty()) {
			return curr;
		}

		// case: a leaf has not been reached and current node is winning
		if (curr->winning) {
			int old_branch = 0;
			if (curr == anchor_node) {
				// compute branching direction from anchor_node to old_memory
				int old_branch = index_below(anchor_node,old_memory);
			}
			// move one to the right, cycling back to 0 if old_branch is rightmost direction
			int next_branch = (old_branch + 1) % (curr->children.size());
			// find leaf below the new branching direction
			return get_leaf(old_memory, anchor_node, curr->children[next_branch], Y);
		} else {
			// find branching direction from curr that allows system choice Y
			int branch = 0;
			while (branch < curr->children.size()) {
				CUDD::BDD restricted = Y * curr->winningmoves[branch];
				if (restricted != var_mgr_->cudd_mgr()->bddZero()) {
					break;
				}
				branch++;
			}
			// find leaf below that branching direction
			return get_leaf(old_memory, anchor_node, curr->children[branch], Y);
		}
	}

	EL_output_function EmersonLei::ExtractStrategy_Explicit(EL_output_function op, CUDD::BDD gameNode, ZielonkaNode *t) const {
		//	t: tree node, s (anchor node): lowest ancester of t that includes all colors of gameNode
		EL_output_function temp = op;
		// stop recursion if the strategy has already been defined for (gameNode,t)
		std::pair<CUDD::BDD, ZielonkaNode*> curr;
		if (temp.find(curr)!=temp.end()) {
					return temp;;
		}

		// the following assumes that system moves first and environment moves second

		ZielonkaNode *s = get_anchor(gameNode, t);

    // BDD that will be used to encode a single choice for system, default bddZero
    CUDD::BDD Y = var_mgr_->cudd_mgr()->bddZero();

		// pick one choice for system that is winning for system from gameNode for objective s
		if (s->children.empty()) {
       		// have just a single winningmoves BDD
					Y = getUniqueSystemChoice(gameNode,s->winningmoves[0]);
    	}
    else {
       	// iterate through all winningmoves BDD until a choice for system is found that is winning from gameNode for objective s; one is guaranteed to be found
			for (int i = 0; i < s->children.size(); i++) {
        Y = getUniqueSystemChoice(gameNode,s->winningmoves[i]);
				if (Y != var_mgr_->cudd_mgr()->bddZero()) {
					break;
				}
			}
		}

		// get next memory value; t: old memory value, s: anchor node, move: system choice that has been picked
		ZielonkaNode *u = get_leaf(t,s,s,Y);

		// add system choice and resulting new memory to extracted strategy, 
		// currently assumes result has component "strategy" which is vector of (gameNode, ZielonkaNode), (CUDD::BDD,ZielonkaNode)
		std::pair<CUDD::BDD, ZielonkaNode*> next;
		next.first = Y;
		next.second = u;
		temp[curr] = next;

		// compute game nodes that can result by taking system choice from gameNode
		//TODO
		std::vector<CUDD::BDD> newGameNodes = getSuccsWithYZ(gameNode,Y);

		// continue strategy construction with each possible new game node and the new memory value
		for (int i = 0; i < newGameNodes.size(); i++) {
			EL_output_function temp_new = ExtractStrategy_Explicit(temp, newGameNodes[i], u);
			temp = temp_new;
		}
		return temp;
	}

	std::vector<CUDD::BDD> EmersonLei::getSuccsWithYZ(CUDD::BDD gameNode, CUDD::BDD Y) const {
		// std::vector<CUDD::BDD> transition_function = spec.transition_function();
		std::vector<CUDD::BDD> transition_vector = transition_function();
		// std::unordered_set<std::string> state_var_names;
		// for (auto state_var_name: var_mgr_->state_variable_labels(spec.automaton_id())) {
		// 		state_var_names.insert(state_var_name);
		// }
		std::vector<CUDD::BDD> succs;
		for (int i = 0; i < transition_vector.size(); i++) {
			CUDD::BDD state_var = var_mgr_->state_variable(spec_id(), i);
			CUDD::BDD transidtion_fix_Y_Z = transition_vector[i] * gameNode * Y;
			if (transidtion_fix_Y_Z == var_mgr_->cudd_mgr()->bddZero()) {
				std::vector<CUDD::BDD> succs_temp;
				for (auto succ: succs) {
					CUDD::BDD succ_new = succ * !state_var;
					succs_temp.push_back(succ_new);
				}
				succs = succs_temp;
			} else if (transidtion_fix_Y_Z == var_mgr_->cudd_mgr()->bddOne()) {
				std::vector<CUDD::BDD> succs_temp;
				for (auto succ: succs) {
					CUDD::BDD succ_new = succ * state_var;
					succs_temp.push_back(succ_new);
				}
				succs = succs_temp;
			} else {
				std::vector<CUDD::BDD> succs_temp;
				for (auto succ: succs) {
					CUDD::BDD succ_new_false = succ * !state_var;
					CUDD::BDD succ_new_true = succ * state_var;
					succs_temp.push_back(succ_new_false);
					succs_temp.push_back(succ_new_true);
				}
				succs = succs_temp;
			}
		}
		return succs;
	}

	CUDD::BDD EmersonLei::getUniqueSystemChoice(CUDD::BDD gameNode, CUDD::BDD winningmoves) const {
		CUDD::BDD restricted = winningmoves * gameNode;
		int n_vars = var_mgr_->total_variable_count();
		int* cube = nullptr;
		CUDD_VALUE_TYPE value;
		DdGen* g = Cudd_FirstCube(restricted.manager(), restricted.getNode(), &cube, &value);
		assert (g != nullptr);
		std::vector<uint8_t> Y_Z_value = std::vector<uint8_t>(cube, cube + n_vars);
		CUDD::BDD Y = var_mgr_->cudd_mgr()->bddOne();
		std::vector<std::string> Y_names = var_mgr_->output_variable_labels();
		for (auto var_name: Y_names) {
			CUDD::BDD var = var_mgr_->name_to_variable(var_name);
			int var_index = var.NodeReadIndex();
			int var_value = static_cast<int>(cube[var_index]);
			if (var_value == 2) {
				continue;
			} else if (var_value == 1) {
				Y = Y * var;
			} else {
				Y = Y * !var;
			}
		}
		return Y;
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
					result = target | project_into_states(new_target_moves);
					// CUDD::BDD diffmoves = (result & (!target) & quantified_X_transitions_to_winning_states);
            		t->winningmoves[i] = t->winningmoves[i] & new_target_moves;
        		} else {
        			CUDD::BDD new_target_moves =
									(state_space_ & (!target) & quantified_X_transitions_to_winning_states);
        			result = target | project_into_states(new_target_moves);
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
            	// t->winningmoves[0]=var_mgr_->cudd_mgr()->bddOne();
	            t->winningmoves.push_back(var_mgr_->cudd_mgr()->bddOne());
            }
            else {
				for (int i = 0; i < t->children.size(); i++) {
                	//t->winningmoves[i] = var_mgr_->cudd_mgr()->bddOne();
		            t->winningmoves.push_back(var_mgr_->cudd_mgr()->bddOne());
				}
			}
  		} else {
            X = var_mgr_->cudd_mgr()->bddZero();
            if (t->children.empty()) { 
            	// t->winningmoves[0]=var_mgr_->cudd_mgr()->bddZero();
	            t->winningmoves.push_back(var_mgr_->cudd_mgr()->bddZero());
            }
            else {
				for (int i = 0; i < t->children.size(); i++) {
                	// t->winningmoves[i] = var_mgr_->cudd_mgr()->bddZero();
		            t->winningmoves.push_back(var_mgr_->cudd_mgr()->bddZero());
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