//
// Created by dh on 25/11/24.
//

#include "game/EmersonLei.hpp"

namespace Syft {
  EmersonLei::EmersonLei(const SymbolicStateDfa &spec, std::string color_formula, Player starting_player,
                         Player protagonist_player,
                         const std::vector<CUDD::BDD> &colorBDDs,
                         const CUDD::BDD &state_space,
                         const CUDD::BDD &instant_winning,
                         const CUDD::BDD &instant_losing)
    : DfaGameSynthesizer(spec, starting_player, protagonist_player), color_formula_(color_formula), Colors_(colorBDDs),
      state_space_(state_space), instant_winning_(instant_winning), instant_losing_(instant_losing) {

    // build Zielonka tree; parse formula from PHI_FILE, number of colors taken from Colors
    z_tree_ = new ZielonkaTree(color_formula_, Colors_, var_mgr_);
  }

  CUDD::BDD EmersonLei::getOneUnprocessedState(CUDD::BDD states, CUDD::BDD processed) const {
    // std::cout << "states: " << states << "\n";
    // std::cout << "processed: " << processed << "\n";

    DdNode *rawNode = (states * (!processed)).getNode();
    // std::cout << "gameNode * winningmoves: " << gameNode * winningmoves << "\n";
    // std::cout << "winningmoves: " << winningmoves << "\n";
    CUDD::BDD Zs(*(var_mgr_->cudd_mgr()), rawNode);
    // std::cout << "All possible Zs: " << Zs << "\n";

    int n_vars = var_mgr_->total_variable_count();
    int *cube = nullptr;
    CUDD_VALUE_TYPE value;
    DdGen *g = Cudd_FirstCube(Zs.manager(), Zs.getNode(), &cube, &value);
    assert(g != nullptr);
    std::vector<uint8_t> Z_value = std::vector<uint8_t>(cube, cube + n_vars);
    CUDD::BDD Z = var_mgr_->cudd_mgr()->bddOne();
    std::vector<std::string> Z_names = var_mgr_->state_variable_labels(spec_.automaton_id());
    for (auto var_name: Z_names) {
      CUDD::BDD var = var_mgr_->name_to_variable(var_name);
      int var_index = var.NodeReadIndex();
      int var_value = static_cast<int>(cube[var_index]);
      if (var_value == 2) {
        // continue;
        Z = Z * var;
      } else if (var_value == 1) {
        Z = Z * var;
      } else {
        Z = Z * !var;
      }
    }
    return Z;
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
    // std::cout << "Colors: \n";
    // for (size_t i = 0; i < Colors_.size(); i++) {
    //   std::cout << Colors_[i] << '\n';
    // }





    // solve EL game for root of Zielonka tree and BDD encoding emptyset as set of states currently assumed to be winning
    CUDD::BDD winning_states = EmersonLeiSolve(z_tree_->get_root(), instant_winning_);
    // std::cout << "winning_states: " << winning_states << std::endl;
    // std::cout << "initial: " << spec_.initial_state_bdd() << "\n";
    // var_mgr_->dump_dot(winning_states.Add(), "winning_states.dot");
    // update result according to computed solution
    ELSynthesisResult result;
    if (includes_initial_state(winning_states)) {
      result.realizability = true;
      result.winning_states = winning_states;
      result.z_tree = z_tree_;
      EL_output_function op;
      
      result.output_function = ExtractStrategy_Explicit(op, winning_states, spec_.initial_state_bdd(),
                                                        z_tree_->get_root());
      return result;
    } else {
      result.realizability = false;
      result.winning_states = winning_states;
      EL_output_function op;

      CUDD::BDD processed = var_mgr_->cudd_mgr()->bddZero();
      while ((winning_states | !processed) != var_mgr_->cudd_mgr()->bddOne()) {
      // while (winning_states.Xnor(processed) != var_mgr_->cudd_mgr()->bddOne()) {
        // std::cout << "winning_states: " << winning_states << "\n";
        // std::cout << "processed: " << processed << "\n";

        CUDD::BDD unprocessed_state = getOneUnprocessedState(winning_states, processed);
        EL_output_function temp = ExtractStrategy_Explicit(op, winning_states, unprocessed_state,
                                                        z_tree_->get_root());
        for (auto item : temp) {
          processed = processed | item.gameNode;
        }
        op = temp;
      }
      result.output_function = op;
      return result;
    }
  }

  //    std::string simplifyFormula(std::vector<std::string> postfix, std::vector<bool> colors){
  //
  // 	// TODO update formula, check postfix format
  //        std::reverse(postfix.begin(), postfix.end());
  //        std::string resFormula;
  //
  //        while (!postfix.empty()){
  //            std::string s = postfix.back();
  //            postfix.pop_back();
  //
  //            if (isNumber(s)){
  //                int tmp = stoi(s);
  //                //std::cout << "var " << tmp << std::endl;
  //                resStack.push_back(colors[tmp]);
  //            }
  //            else{
  //                if (s == "!"){
  //                    bool tmp = resStack.back();
  //                    resStack.pop_back();
  //                    //std::cout << "not " << tmp << std::endl;
  //                    resStack.push_back(!tmp);
  //                }
  //                else if (s == "&"){
  //                    bool tmp1 = resStack.back();
  //                    resStack.pop_back();
  //                    bool tmp2 = resStack.back();
  //                    resStack.pop_back();
  //                    //std::cout << tmp1 << " & " << tmp2 << std::endl;
  //                    resStack.push_back(tmp1 && tmp2);
  //                }
  //                else{
  //                    bool tmp1 = resStack.back();
  //                    resStack.pop_back();
  //                    bool tmp2 = resStack.back();
  //                    resStack.pop_back();
  //                    //std::cout << tmp1 << " & " << tmp2 << std::endl;
  //                    resStack.push_back(tmp1 || tmp2);
  //                }
  //            }
  //        }
  //        if (resStack.size() != 1)
  //            std::cout << "resStack wrong size" << std::endl;
  //        return resStack.back();
  //    }
  //
  //
  // ELSynthesisResult EmersonLei::run_MP() const {
  // 	//TODO
  // 	// create another class LTLfPlusSynthesizerMP
  // 	// create another class MannaPnueli game
  // 	// in the MP game, we need to store the color formula as a BDD, which can be simplified given specific values of Fcolors and Gcolors
  // 	// we also need some structure to store the DAG, and the MP game starts from the bottom node in the DAG, this is obtained from the color formula
  // 	// an EL game should take ZielonkaTree node, an instantWinning BDD and an instantLosing BDD
  // 	// the result of MP game should be a different structure such that we have a vector of winningstates, every winningstates BDD corresponds to a game in the DAG
  //
  //    std::cout << "Colors: \n";
  //    for (size_t i = 0; i < Colors_.size(); i++){
  //        std::cout << Colors_[i] << '\n';
  //    }
  //
  // 	std::unordered_map<std::size_t, bool> Fcolors, Gcolors;
  // 	std::vector<bool> curFcolors(Fcolors.size(), true);
  // 	std::vector<bool> curGcolors(Gcolors.size(), false);
  //
  // 	std::queue<std::pair(std::vector<bool>, std::vector<bool>)> todo;
  // 	todo.push(curFcolors,curGcolors);
  //
  //
  // 	while (!todo.empty()) {
  //
  // 		(curFcolors,curGcolors) = todo.pop();
  //
  // 		// curcolors should be the OR of the two bitvectors curFcolors and curGcolors
  // 		curcolors = curFcolors|curGcolors; // concatnate Fcolors (first) and Gcolors
  //
  // 		// instantiate all Fc and all Gc in color_formula to 1 if c is in curcolors and to 0 otherwise
  // 		std::string curColor_formula = simplifyFormula(ELHelpers::infix2postfix(ELHelpers::tokenize(color_formula_)),curcolors);
  //
  // 		// build Zielonka tree for current F- and G-colors
  // 		ZielonkaTree *Ztree = new ZielonkaTree(curColor_formula, Colors_, var_mgr_);
  //
  // 		// setup winning and losing nodes
  // 		CUDD::BDD instantWinning;
  // 		CUDD::BDD instantLosing;
  //
  // 		// TODO: loop over existing entries in the vector result.winning_states; each entry is a pair (colors,winningStates).
  // 		// 		 Add nodes from winningStates for which curcolors&seencolors=colors to instantWinning
  // 		//		 Add nodes from !winningStates for which curcolors&seencolors=colors to instantLosing
  //
  // 		// solve EL game for root of Zielonka tree
  // 		CUDD::BDD winning_states = EmersonLeiSolve(Ztree->get_root(), instantWinning, instantLosing);
  //
  // 		// update result according to computed solution, TODO: store result for curcolors; also, winningmoves
  // 		ELSynthesisResult result;
  // 		if (includes_initial_state(winning_states)) {
  // 			result.realizability = true;
  // 			result.winning_states = winning_states;
  // 			EL_output_function op;
  // 			result.output_function = ExtractStrategy_Explicit(op, spec_.initial_state_bdd(), Ztree->get_root());
  // 			return result;
  // 		} else {
  // 			result.realizability = false;
  // 			result.winning_states = winning_states;
  // 			EL_output_function output_function;
  // 			result.output_function = output_function;
  // 			return result;
  // 		}
  //
  //
  // 		//TODO
  // 		//Store the DAG
  //
  // 		// for any color i in curFcolors, push (curFcolors-i,curGcolors) to queue
  // 		for (int i = 0; i < curFcolors.size(); i++) {
  // 			if (curFcolors[i]) {
  // 				curFcolors[i] = false;
  // 				todo.push(curFcolors,curGcolors);
  // 				curFcolors[i] = true;
  // 			}
  // 		}
  //
  // 		// for any color i in curGcolors, push (curFcolors,curGcolors-i) to queue
  // 		for (int i = 0; i < curGcolors.size(); i++) {
  // 			if (!curGcolors[i]) {
  // 				curGcolors[i] = true;
  // 				todo.push(curFcolors,curGcolors);
  // 				curFcolors[i] = false;
  // 			}
  // 		}
  //
  // 		// note: this will have duplicate computations if the same set of curcolors can be reached by removing single colors in different ways, so before computation, need lookup.
  //
  //
  // 	}
  //
  // }


  int EmersonLei::index_below(ZielonkaNode *anchor_node, ZielonkaNode *old_memory) const {
    if (old_memory == anchor_node) {
      return 0;
    } else {
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

  ZielonkaNode *EmersonLei::get_anchor(CUDD::BDD game_node, ZielonkaNode *t) const {
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

  ZielonkaNode *EmersonLei::get_leaf(ZielonkaNode *old_memory, ZielonkaNode *anchor_node, ZielonkaNode *curr,
                                     CUDD::BDD Y) const {
    // case: a leaf has been reached
    if (curr->children.empty()) {
      return curr;
    }

    // case: a leaf has not been reached and current node is winning
    if (curr->winning) {
      int old_branch = 0;
      if (curr == anchor_node) {
        // compute branching direction from anchor_node to old_memory
        old_branch = index_below(anchor_node, old_memory);
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

  EL_output_function EmersonLei::ExtractStrategy_Explicit(EL_output_function op, CUDD::BDD winning_states,
                                                          CUDD::BDD gameNode, ZielonkaNode *t) const {

    //	t: tree node, s (anchor node): lowest ancester of t that includes all colors of gameNode
    EL_output_function temp = op;
    // stop recursion if the strategy has already been defined for (gameNode,t)

    // std::cout << "-----------\ngameNode: " << gameNode;
    // gameNode.PrintCover();
    // std::cout << "tree node: " << t->order << "\n";

    std::pair<CUDD::BDD, ZielonkaNode *> curr;
    curr.first = gameNode;
    curr.second = t;
    for (auto item: op) {
      // std::cout << item.gameNode << " " << item.t->order << "\n";
      // std::cout << item.Y << " " << item.u->order << "\n";
      if ( (item.t->order == t->order)) {
        // if ((item.gameNode.Xnor(gameNode) == var_mgr_->cudd_mgr()->bddOne())) {
        if ((item.gameNode | !(gameNode)) == var_mgr_->cudd_mgr()->bddOne()) {
          // std::cout << "defined! " << gameNode << " " << t->order << "\n";
          // gameNode.PrintCover();
          // std::cout << "stored " << item.gameNode << " " << item.t->order << "\n";
          // item.gameNode.PrintCover();
          return temp;
        }
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
    } else {
      // iterate through all winningmoves BDD until a choice for system is found that is winning from gameNode for objective s; one is guaranteed to be found
      for (int i = 0; i < s->children.size(); i++) {
        Y = getUniqueSystemChoice(gameNode, s->winningmoves[i]);
        if (Y != var_mgr_->cudd_mgr()->bddZero()) {
          break;
        }
      }
    }

    // get next memory value; t: old memory value, s: anchor node, move: system choice that has been picked
    ZielonkaNode *u = get_leaf(t, s, s, Y);

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
    std::vector<CUDD::BDD> newGameNodes = getSuccsWithYZ(gameNode, Y);

    // continue strategy construction with each possible new game node and the new memory value
    for (int i = 0; i < newGameNodes.size(); i++) {
      EL_output_function temp_new = ExtractStrategy_Explicit(temp, winning_states, newGameNodes[i], u);
      temp = temp_new;
    }
    return temp;
  }
  /*
  EmersonLei::OneStepSynReturn EmersonLei::ExtractStrategy_Explicit_OneStep(EL_output_function op, CUDD::BDD winning_states,
                                                          CUDD::BDD gameNode, ZielonkaNode *t, CUDD::BDD X) const {
    //	t: tree node, s (anchor node): lowest ancester of t that includes all colors of gameNode
    EL_output_function temp = op;
    // stop recursion if the strategy has already been defined for (gameNode,t)

    // std::cout << "-----------\ngameNode: " << gameNode;
    // gameNode.PrintCover();
    // std::cout << "tree node: " << t->order << "\n";

    std::pair<CUDD::BDD, ZielonkaNode *> curr;
    curr.first = gameNode;
    curr.second = t;
    for (auto item: op) {
      // std::cout << item.gameNode << " " << item.t->order << "\n";
      // std::cout << item.Y << " " << item.u->order << "\n";
      if ((item.gameNode.Xnor(gameNode) == var_mgr_->cudd_mgr()->bddOne()) && (item.t->order == t->order)) {
        // std::cout << "defined! " << gameNode << " " << t->order << "\n";
        // gameNode.PrintCover();
        // std::cout << "stored " << item.gameNode << " " << item.t->order << "\n";
        // item.gameNode.PrintCover();
        OneStepSynReturn res;
        res.op_ = op;
        res.game_state_ = gameNode;
        res.tree_node_ = t->order;
        res.Y_ = item.Y;
        return res;
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
    } else {
      // iterate through all winningmoves BDD until a choice for system is found that is winning from gameNode for objective s; one is guaranteed to be found
      for (int i = 0; i < s->children.size(); i++) {
        Y = getUniqueSystemChoice(gameNode, s->winningmoves[i]);
        if (Y != var_mgr_->cudd_mgr()->bddZero()) {
          break;
        }
      }
    }

    // get next memory value; t: old memory value, s: anchor node, move: system choice that has been picked
    ZielonkaNode *u = get_leaf(t, s, s, Y);

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
    CUDD::BDD newGameNode = getSuccsWithXYZ(gameNode, Y, X);


    EL_output_function temp_new = ExtractStrategy_Explicit(temp, winning_states, newGameNode, u);
    OneStepSynReturn res;
    res.op_ = temp_new;
    res.game_state_ = newGameNode;
    res.tree_node_ = u->order;
    res.Y_ = Y;

    return res;
  }
*/
  CUDD::BDD EmersonLei::getSuccsWithXYZ(CUDD::BDD gameNode, CUDD::BDD Y, CUDD::BDD X) const {
    std::vector<CUDD::BDD> succs;
    std::vector<CUDD::BDD> transition_vector = transition_function();
    std::vector<CUDD::BDD> transition_vector_fix_Y_Z;
    for (int i = 0; i < transition_vector.size(); i++) {
      CUDD::BDD transition_fix_Y_Z = (transition_vector[i] * gameNode * Y * X).ExistAbstract(
        var_mgr_->state_variables_cube(spec_id())).ExistAbstract(var_mgr_->output_cube()).ExistAbstract(var_mgr_->input_cube());
      transition_vector_fix_Y_Z.push_back(transition_fix_Y_Z);
    }

    CUDD::BDD succ = var_mgr_->cudd_mgr()->bddOne();
    for (int i = 0; i < transition_vector_fix_Y_Z.size(); i++) {
      CUDD::BDD Z_var = var_mgr_->state_variable(spec_id(), i);
      if (transition_vector_fix_Y_Z[i].IsOne()) {
        succ = succ * Z_var;
      } else {
        succ = succ * !Z_var;
      }
    }
    return succ;
  }

  std::vector<CUDD::BDD> EmersonLei::getSuccsWithYZ(CUDD::BDD gameNode, CUDD::BDD Y) const {
    std::vector<CUDD::BDD> succs;
    std::vector<CUDD::BDD> transition_vector = transition_function();
    std::vector<CUDD::BDD> transition_vector_fix_Y_Z;
    for (int i = 0; i < transition_vector.size(); i++) {
      CUDD::BDD transition_fix_Y_Z = (transition_vector[i] * gameNode * Y).ExistAbstract(
        var_mgr_->state_variables_cube(spec_id())).ExistAbstract(var_mgr_->output_cube());
      transition_vector_fix_Y_Z.push_back(transition_fix_Y_Z);
    }
    std::vector<std::string> X_labels = var_mgr_->input_variable_labels();
    int total = 1 << X_labels.size(); // 2^n
    for (int mask = 0; mask < total; ++mask) {
      std::vector<int> values(var_mgr_->total_variable_count(), 0);
      for (int i = 0; i < X_labels.size(); ++i) {
        CUDD::BDD X_var = var_mgr_->name_to_variable(X_labels[i]);
        values[X_var.NodeReadIndex()] = (mask >> i) & 1; // Extract the i-th bit
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
    DdNode *rawNode = Cudd_bddRestrict(var_mgr_->cudd_mgr()->getManager(), winningmoves.getNode(), gameNode.getNode());
    // std::cout << "gameNode * winningmoves: " << gameNode * winningmoves << "\n";
    // std::cout << "winningmoves: " << winningmoves << "\n";
    CUDD::BDD Ys(*(var_mgr_->cudd_mgr()), rawNode);
    // std::cout << "All possible Ys: " << Ys << "\n";
    int n_vars = var_mgr_->total_variable_count();
    int *cube = nullptr;
    CUDD_VALUE_TYPE value;
    DdGen *g = Cudd_FirstCube(Ys.manager(), Ys.getNode(), &cube, &value);
    assert(g != nullptr);
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
      // std::cout << "quantified_X_transitions_to_winning_states: " << quantified_X_transitions_to_winning_states << std::endl;
      //             CUDD::BDD new_target_moves = target |
      //                                 (state_space_ & (!target) & quantified_X_transitions_to_winning_states);
      //             result = project_into_states(new_target_moves);
      // CUDD::BDD diffmoves = (result & (!target) & quantified_X_transitions_to_winning_states);
      if (t->winning) {
        CUDD::BDD new_target_moves = (state_space_ & quantified_X_transitions_to_winning_states) & (!instant_losing_);
        result = project_into_states(new_target_moves);
        // std::cout << "project into states: " << project_into_states(new_target_moves) << std::endl;
        // CUDD::BDD diffmoves = (result & (!target) & quantified_X_transitions_to_winning_states);
        t->winningmoves[i] = t->winningmoves[i] & new_target_moves;
      } else {
        CUDD::BDD new_target_moves_with_loops =
            (state_space_ & quantified_X_transitions_to_winning_states) & (!instant_losing_);
        CUDD::BDD new_target_moves = (!target) & new_target_moves_with_loops;
        result = project_into_states(new_target_moves_with_loops);
        // std::cout << "project into states: " << project_into_states(new_target_moves) << std::endl;
        // CUDD::BDD diffmoves = (result & (!target) & quantified_X_transitions_to_winning_states);
        t->winningmoves[i] = t->winningmoves[i] | new_target_moves;
      }
    } else {
      //TODO need to double-check
      CUDD::BDD transitions_to_target_states = preimage(target);
      if (t->winning) {
        result = state_space_ & project_into_states(transitions_to_target_states);
        // result = target | new_collected_target_states;
        CUDD::BDD new_target_moves = (result & transitions_to_target_states) & (!instant_losing_);
        // CUDD::BDD diffmoves = (!target) & transitions_to_target_states;
        t->winningmoves[i] = t->winningmoves[i] & new_target_moves;
      } else {
        result = state_space_ & project_into_states(transitions_to_target_states);
        // result = target | new_collected_target_states;
        CUDD::BDD new_target_moves = (!target) & result & transitions_to_target_states & (!instant_losing_);
        t->winningmoves[i] = t->winningmoves[i] | new_target_moves;
      }
    }
    // std::cout << "cpre: " << result << "\n";
    return result;
  }

  CUDD::BDD EmersonLei::EmersonLeiSolve(ZielonkaNode *t, CUDD::BDD term) const {
    // std::cout << "term: " << term << std::endl;
    CUDD::BDD X, XX;

    // initialize variables for fixpoint computation (gfp for winning / lfp for losing)
    if (t->winning) {
      X = var_mgr_->cudd_mgr()->bddOne();
      if (t->children.empty()) {
        // t->winningmoves[0]=var_mgr_->cudd_mgr()->bddOne();
        // t->winningmoves.push_back(var_mgr_->cudd_mgr()->bddOne());
        t->winningmoves.push_back(!instant_losing_);
      } else {
        for (int i = 0; i < t->children.size(); i++) {
          //t->winningmoves[i] = var_mgr_->cudd_mgr()->bddOne();
          // t->winningmoves.push_back(var_mgr_->cudd_mgr()->bddOne());
          t->winningmoves.push_back(!instant_losing_);
        }
      }
    } else {
      X = var_mgr_->cudd_mgr()->bddZero();
      if (t->children.empty()) {
        // t->winningmoves[0]=var_mgr_->cudd_mgr()->bddZero();
        t->winningmoves.push_back(var_mgr_->cudd_mgr()->bddZero());
      } else {
        for (int i = 0; i < t->children.size(); i++) {
          // t->winningmoves[i] = var_mgr_->cudd_mgr()->bddZero();
          t->winningmoves.push_back(var_mgr_->cudd_mgr()->bddZero());
        }
      }
    }

    // loop until fixpoint has stabilized
    while (true) {
      // std::cout << "Node: " << t->order << "\n";
      // std::cout << X << "\n";

      // if t is a leaf
      if (t->children.empty()) {
        // std::cout << "cpre:" << cpre(t, 0, X & (!instant_losing_)) << "\n";
        // std::cout << "t->safenodes:" <<  t->safenodes << "\n";
        XX = term | (t->safenodes & cpre(t, 0, X & (!instant_losing_)));
      }

      // if t is not a leaf
      else {
        // initialize intersecion for winning / union for losing
        if (t->winning) {
          XX = var_mgr_->cudd_mgr()->bddOne();
        } else {
          XX = var_mgr_->cudd_mgr()->bddZero();
        }

        // iterate over direct children of t
        // for (auto s : t->children) {
        for (int i = 0; i < t->children.size(); i++) {
          // add new choice to term
          auto s = t->children[i];
          CUDD::BDD current_term = term | (s->targetnodes & cpre(t, i, X & (!instant_losing_)));
          if (t->winning) {
            // intersect with recursively computed solution for s and current term
            XX &= EmersonLeiSolve(s, current_term);
          } else {
            // union with recursively computed solution for s and current term
            XX |= EmersonLeiSolve(s, current_term);
          }
        }
      }
      // std::cout << "X: " << X << std::endl;
      // std::cout << "XX: " << XX << std::endl;

      if (X == XX) {
        break;
      } else {
        X = XX;
      }
    }

    // return stabilized fixpoint
    return X;
  }

  // EmersonLei::OneStepSynReturn EmersonLei::synthesize(std::string X, ELSynthesisResult result) {
  //   if (syn_flag_ == false) {
  //     syn_flag_ = true;
  //     curr_state_.emplace(spec_.initial_state_bdd());
  //     curr_tree_node_.emplace(z_tree_->get_root());
  //   } else {
  //     CUDD::BDD X;
  //     //TODO = ExtractStrategy_Explicit_OneStep(result.output_function, result.winning_states, curr_state_.value(), curr_tree_node_.value(), X);
  //   }
  //   OneStepSynReturn result;
  //
  // }

}
