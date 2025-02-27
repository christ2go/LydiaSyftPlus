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
			std::cout << "initial: " << spec_.initial_state_bdd() << "\n";
			result.output_function = ExtractStrategy_Explicit(op, winning_states, spec_.initial_state_bdd(), Ztree->get_root());
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

	ZielonkaNode* EmersonLei::get_anchor(CUDD::BDD game_node, ZielonkaNode *t) const {
		if (t->order == 1) {
			return t;
		}
		CUDD::BDD restricted = game_node * t->targetnodes;
		if (restricted != var_mgr_->cudd_mgr()->bddZero()) {
			return t->parent;
		} else {
			return (get_anchor(game_node, t->parent));
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
				old_branch = index_below(anchor_node,old_memory);
			}
			// move one to the right, cycling back to 0 if old_branch is rightmost direction
			int next_branch = (old_branch + 1) % (curr->children.size());
			// find leaf below the new branching direction
			return get_leaf(old_memory, anchor_node, curr->children[next_branch], Y);
		} else {
			// find branching direction from curr that allows system choice Y
			int branch = 0;
			while (branch < curr->children.size()) {
				CUDD::BDD restricted = (Y * curr->winningmoves[branch]).ExistAbstract(var_mgr_->output_cube());
				if (restricted != var_mgr_->cudd_mgr()->bddZero()) {
					break;
				}
				branch++;
			}
			// find leaf below that branching direction
			return get_leaf(old_memory, anchor_node, curr->children[branch], Y);
		}
	}

	EL_output_function EmersonLei::ExtractStrategy_Explicit(EL_output_function op, CUDD::BDD winning_states, CUDD::BDD gameNode, ZielonkaNode *t) const {
		//	t: tree node, s (anchor node): lowest ancester of t that includes all colors of gameNode
		EL_output_function temp = op;
		// stop recursion if the strategy has already been defined for (gameNode,t)

		// std::cout << "-----------\ngameNode: " << gameNode;
		// gameNode.PrintCover();
		// std::cout << "tree node: " << t->order << "\n";

		std::pair<CUDD::BDD, ZielonkaNode*> curr;
		curr.first = gameNode;
		curr.second = t;
		for (auto item : op) {
			// std::cout << item.gameNode << " " << item.t->order << "\n";
			// std::cout << item.Y << " " << item.u->order << "\n";
			if ((item.gameNode.Xnor(gameNode) == var_mgr_->cudd_mgr()->bddOne()) && (item.t->order == t->order)) {
				// std::cout << "defined! " << gameNode << " " << t->order << "\n";
				// gameNode.PrintCover();
				// std::cout << "stored " << item.gameNode << " " << item.t->order << "\n";
				// item.gameNode.PrintCover();
				return temp;
			}
		}

		// the following assumes that system moves first and environment moves second

		ZielonkaNode *s = get_anchor(gameNode, t);

    // BDD that will be used to encode a single choice for system, default bddZero
    CUDD::BDD Y = var_mgr_->cudd_mgr()->bddZero();

		// pick one choice for system that is winning for system from gameNode for objective s
		if (s->children.empty()) {
       		// have just a single winningmoves BDD
				Y = getUniqueSystemChoice(gameNode, s->winningmoves[0]);
				// Y = getUniqueSystemChoice(gameNode,s->winningmoves[0]);
    	}
    else {
       	// iterate through all winningmoves BDD until a choice for system is found that is winning from gameNode for objective s; one is guaranteed to be found
			for (int i = 0; i < s->children.size(); i++) {
				Y = getUniqueSystemChoice(gameNode, s->winningmoves[i]);
				if (Y != var_mgr_->cudd_mgr()->bddZero()) {
					break;
				}
			}
		}

		// get next memory value; t: old memory value, s: anchor node, move: system choice that has been picked
		ZielonkaNode *u = get_leaf(t,s,s,Y);

		// add system choice and resulting new memory to extracted strategy, 
		// currently assumes result has component "strategy" which is vector of (gameNode, ZielonkaNode), (CUDD::BDD,ZielonkaNode)
		ELWinningMove move;
		move.gameNode = gameNode;
		move.t = t;
		move.Y = Y;
		move.u = u;
		temp.push_back(move);
		// std::cout << " --> \n";
		// std::cout << "Y: " << Y << "\n";
		// std::cout << "tree node: " << u->order << "\n\n";

		// compute game nodes that can result by taking system choice from gameNode
		//TODO
		std::vector<CUDD::BDD> newGameNodes = getSuccsWithYZ(gameNode,Y);

		// continue strategy construction with each possible new game node and the new memory value
		for (int i = 0; i < newGameNodes.size(); i++) {
			EL_output_function temp_new = ExtractStrategy_Explicit(temp, winning_states, newGameNodes[i], u);
			temp = temp_new;
		}
		return temp;
	}

	std::vector<CUDD::BDD> EmersonLei::getSuccsWithYZ(CUDD::BDD gameNode, CUDD::BDD Y) const {
		std::vector<CUDD::BDD> succs;
		std::vector<CUDD::BDD> transition_vector = transition_function();
		std::vector<CUDD::BDD> transition_vector_fix_Y_Z;
		for (int i = 0; i < transition_vector.size(); i++) {
			CUDD::BDD transition_fix_Y_Z = (transition_vector[i] * gameNode * Y).ExistAbstract(var_mgr_->state_variables_cube(spec_id())).ExistAbstract(var_mgr_->output_cube());
			transition_vector_fix_Y_Z.push_back(transition_fix_Y_Z);
		}
		std::vector<std::string> X_labels = var_mgr_->input_variable_labels();
		int total = 1 << X_labels.size();  // 2^n
		for (int mask = 0; mask < total; ++mask) {
			std::vector<int> values(var_mgr_->total_variable_count(), 0);
			for (int i = 0; i < X_labels.size(); ++i) {
				CUDD::BDD X_var = var_mgr_->name_to_variable(X_labels[i]);
				values[X_var.NodeReadIndex()] = (mask >> i) & 1;  // Extract the i-th bit
			}
			std::vector<int> copy(values);
			CUDD::BDD succ = var_mgr_->cudd_mgr()->bddOne();
			for (int i = 0; i < transition_vector_fix_Y_Z.size(); i++) {
				CUDD::BDD Z_var = var_mgr_->state_variable(spec_id(), i);
				if (transition_vector_fix_Y_Z[i].Eval(copy.data()).IsOne()) {
					succ = succ * Z_var;
				} else {
					succ = succ * !Z_var;
				}
			}
			succs.push_back(succ);
		}
		return succs;
	}


	CUDD::BDD EmersonLei::getUniqueSystemChoice(CUDD::BDD gameNode, CUDD::BDD winningmoves) const {
		DdNode* rawNode = Cudd_bddRestrict(var_mgr_->cudd_mgr()->getManager(), winningmoves.getNode(), gameNode.getNode());
		// std::cout << "gameNode * winningmoves: " << gameNode * winningmoves << "\n";
		// std::cout << "winningmoves: " << winningmoves << "\n";
		CUDD::BDD Ys(*(var_mgr_->cudd_mgr()), rawNode);
		// std::cout << "All possible Ys: " << Ys << "\n";
		int n_vars = var_mgr_->total_variable_count();
		int* cube = nullptr;
		CUDD_VALUE_TYPE value;
		DdGen* g = Cudd_FirstCube(Ys.manager(), Ys.getNode(), &cube, &value);
		assert (g != nullptr);
		std::vector<uint8_t> Y_Z_value = std::vector<uint8_t>(cube, cube + n_vars);
		CUDD::BDD Y = var_mgr_->cudd_mgr()->bddOne();
		std::vector<std::string> Y_names = var_mgr_->output_variable_labels();
		for (auto var_name: Y_names) {
			CUDD::BDD var = var_mgr_->name_to_variable(var_name);
			int var_index = var.NodeReadIndex();
			int var_value = static_cast<int>(cube[var_index]);
			if (var_value == 2) {
				// continue;
				Y = Y * var;
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
			} else {
 		  		X = XX;
			}

		
        }

	    // return stabilized fixpoint
        return X;

	}

	
}