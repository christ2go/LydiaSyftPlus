//
// Created by Shufang Zhu on 05/03/2025.
//

#include "game/MannaPnueli.hpp"
#include "game/EmersonLei.hpp"
#include <iostream>
#include <cuddObj.hh>
#include <stack>
#include <string>
#include <map>
#include <cctype>
#include <sstream>
#include <regex>
#include <queue>

namespace Syft {
  MannaPnueli::MannaPnueli(const SymbolicStateDfa &spec, const std::string color_formula, std::vector<int> F_colors,
                           std::vector<int> G_colors, Player starting_player,
                           Player protagonist_player,
                           const std::vector<CUDD::BDD> &colorBDDs,
                           const CUDD::BDD &state_space)
    : DfaGameSynthesizer(spec, starting_player, protagonist_player), color_formula_(color_formula),
      F_colors_(F_colors), G_colors_(G_colors), Colors_(colorBDDs), state_space_(state_space) {
    color_mgr_ = CUDD::Cudd();
    color_formula_bdd_ = boolean_string_to_bdd(color_formula_);
    std::cout << "Mapping of integer propositions to BDD variables:" << std::endl;
    for (const auto &pair: color_to_variable_) {
      std::cout << "Color " << pair.first << " -> BDD ID: " << pair.second.NodeReadIndex() << std::endl;
    }

    std::cout << "Mapping of BDD variables ID to integer propositions:" << std::endl;
    for (const auto &pair: bdd_id_to_color_) {
      std::cout << "Color " << pair.first << " -> BDD ID: " << pair.second << std::endl;
    }
    tie(dag_, node_to_id_) = build_FG_dag();
    print_FG_dag();
  }

  void MannaPnueli::print_FG_dag() const {
    std::cout << "EL DAG:\n";
    for (auto &[id, node]: dag_) {
      std::cout << "Dag Node " << node->id << " (";
      for (int bit: node->F) std::cout << bit;
      std::cout << ", ";
      for (int bit: node->G) std::cout << bit;
      std::cout << ") <- {";
      for (Node *parent: node->parents) {
        std::cout << " Dag Node " << parent->id << " (";
        for (int bit: parent->F) std::cout << bit;
        std::cout << ", ";
        for (int bit: parent->G) std::cout << bit;
        std::cout << ") ";
      }
      std::cout << "}\n";
      std::cout << "-> {";
      for (auto child: node->children) {
        std::cout << " Node " << child.first->id << " (";
        for (int bit: child.first->F) std::cout << bit;
        std::cout << ", ";
        for (int bit: child.first->G) std::cout << bit;
        std::cout << ") ";
        std::cout << child.second << " ";
      }
      std::cout << "}\n";
    }
  }

  SynthesisResult MannaPnueli::run() const {
    SynthesisResult result;
    result.realizability = true;
    result.winning_states = var_mgr_->cudd_mgr()->bddZero();
    result.winning_moves = var_mgr_->cudd_mgr()->bddZero();
    result.transducer = nullptr;
    return result;
  }

  //   std::string simplifyFormula(std::vector<std::string> postfix, std::vector<bool> colors){
  //
  // // TODO update formula, check postfix format
  //       std::reverse(postfix.begin(), postfix.end());
  //       std::string resFormula;
  //
  //       while (!postfix.empty()){
  //           std::string s = postfix.back();
  //           postfix.pop_back();
  //
  //           if (isNumber(s)){
  //               int tmp = stoi(s);
  //               //std::cout << "var " << tmp << std::endl;
  //               resStack.push_back(colors[tmp]);
  //           }
  //           else{
  //               if (s == "!"){
  //                   bool tmp = resStack.back();
  //                   resStack.pop_back();
  //                   //std::cout << "not " << tmp << std::endl;
  //                   resStack.push_back(!tmp);
  //               }
  //               else if (s == "&"){
  //                   bool tmp1 = resStack.back();
  //                   resStack.pop_back();
  //                   bool tmp2 = resStack.back();
  //                   resStack.pop_back();
  //                   //std::cout << tmp1 << " & " << tmp2 << std::endl;
  //                   resStack.push_back(tmp1 && tmp2);
  //               }
  //               else{
  //                   bool tmp1 = resStack.back();
  //                   resStack.pop_back();
  //                   bool tmp2 = resStack.back();
  //                   resStack.pop_back();
  //                   //std::cout << tmp1 << " & " << tmp2 << std::endl;
  //                   resStack.push_back(tmp1 || tmp2);
  //               }
  //           }
  //       }
  //       if (resStack.size() != 1)
  //           std::cout << "resStack wrong size" << std::endl;
  //       return resStack.back();
  //   }

  std::string MannaPnueli::remove_whitespace(const std::string &str) const {
    std::string result;
    for (char ch: str) {
      if (!isspace(ch)) result += ch;
    }
    return result;
  }

  int MannaPnueli::precedence(char op) const {
    if (op == '!') return 3; // Highest precedence (Unary NOT)
    if (op == '&') return 2; // AND
    if (op == '|') return 1; // OR
    return 0;
  }

  // Convert infix expression to postfix using Shunting-yard algorithm
  std::string MannaPnueli::infix_to_postfix(const std::string &infix) const {
    std::string postfix;
    std::stack<char> ops;

    for (size_t i = 0; i < infix.size(); i++) {
      char ch = infix[i];

      if (isdigit(ch)) {
        postfix += ch;
        // Check if it's a multi-digit number
        while (i + 1 < infix.size() && isdigit(infix[i + 1])) {
          postfix += infix[++i];
        }
        postfix += ' '; // Space as delimiter
      } else if (ch == '(') {
        ops.push(ch);
      } else if (ch == ')') {
        while (!ops.empty() && ops.top() != '(') {
          postfix += ops.top();
          postfix += ' ';
          ops.pop();
        }
        ops.pop(); // Remove '('
      } else if (ch == '!' || ch == '&' || ch == '|') {
        while (!ops.empty() && precedence(ops.top()) >= precedence(ch)) {
          postfix += ops.top();
          postfix += ' ';
          ops.pop();
        }
        ops.push(ch);
      }
    }

    while (!ops.empty()) {
      postfix += ops.top();
      postfix += ' ';
      ops.pop();
    }

    return postfix;
  }

  // Function to construct BDD from postfix expression
  CUDD::BDD MannaPnueli::boolean_string_to_bdd(const std::string &color_formula) {
    std::string formula = remove_whitespace(color_formula);
    std::string postfix = infix_to_postfix(formula);

    std::stack<CUDD::BDD> bdd_stack;
    std::stringstream ss(postfix);
    std::string token;

    while (ss >> token) {
      if (isdigit(token[0])) {
        int var = stoi(token);
        // If variable is not already created, create it
        if (color_to_variable_.find(var) == color_to_variable_.end()) {
          CUDD::BDD var_bdd = color_mgr_.bddVar();
          color_to_variable_[var] = var_bdd;
          bdd_id_to_color_[var_bdd.NodeReadIndex()] = var;
        }
        bdd_stack.push(color_to_variable_[var]);
      } else if (token == "!") {
        CUDD::BDD operand = bdd_stack.top();
        bdd_stack.pop();
        bdd_stack.push(!operand);
      } else if (token == "&") {
        CUDD::BDD right = bdd_stack.top();
        bdd_stack.pop();
        CUDD::BDD left = bdd_stack.top();
        bdd_stack.pop();
        bdd_stack.push(left & right);
      } else if (token == "|") {
        CUDD::BDD right = bdd_stack.top();
        bdd_stack.pop();
        CUDD::BDD left = bdd_stack.top();
        bdd_stack.pop();
        bdd_stack.push(left | right);
      }
    }

    return bdd_stack.top();
  }

  std::string MannaPnueli::color_formula_bdd_to_string(const CUDD::BDD &color_formula_bdd) const {
    std::string formula_str = color_formula_bdd.FactoredFormString();
    formula_str = std::regex_replace(formula_str, std::regex("x(\\d+)"), "$1");

    // replace the bdd ID with their corresponding color
    std::regex prop_regex(R"(\b\d+\b)"); // Matches whole numbers
    std::sregex_iterator it(formula_str.begin(), formula_str.end(), prop_regex);
    std::sregex_iterator end;

    for (; it != end; ++it) {
      int prop = std::stoi(it->str()); // Convert matched string to integer
      if (bdd_id_to_color_.find(prop) != bdd_id_to_color_.end()) {
        formula_str = std::regex_replace(formula_str, std::regex("\\b" + std::to_string(prop) + "\\b"),
                                         std::to_string(bdd_id_to_color_.at(prop)));
      }
    }

    return formula_str;
  }

  std::pair<MannaPnueli::Dag, MannaPnueli::Node_to_Id> MannaPnueli::build_FG_dag() {
    Dag dag;
    Node_to_Id node_to_id;
    int m = F_colors_.size();
    int n = G_colors_.size();
    int nodeCounter = 0; // Sizes of F and G, and a counter for node IDs
    // Map (F, G) states to node IDs
    std::queue<Node *> q;
    std::vector<int> initialF(m, 1), initialG(n, 0);
    Node *root = new Node{initialF, initialG, nodeCounter++};
    dag[root->id] = root;
    node_to_id[{initialF, initialG}] = root->id;
    q.push(root);

    while (!q.empty()) {
      Node *node = q.front();
      q.pop();

      int x = count(node->F.begin(), node->F.end(), 1);
      int y = count(node->G.begin(), node->G.end(), 0);

      if (x == 0 && y == 0) continue; // Stop when F is all 0s and G is all 1s

      // Generate all possible children by reducing different 1s in F
      for (int i = 0; i < m; i++) {
        if (node->F[i] == 1) {
          std::vector<int> newF = node->F;
          newF[i] = 0;
          if (node_to_id.find({newF, node->G}) == node_to_id.end()) {
            Node *newNode = new Node{newF, node->G, nodeCounter++};
            dag[newNode->id] = newNode;
            node_to_id[{newF, node->G}] = newNode->id;
            q.push(newNode);
          }
          Node *parent_node = dag[node_to_id[{newF, node->G}]];
          node->parents.push_back(parent_node);
          parent_node->children.push_back({node, F_colors_[i]});
        }
      }

      // Generate all possible children by reducing different 0s in G
      for (int i = 0; i < n; i++) {
        if (node->G[i] == 0) {
          std::vector<int> newG = node->G;
          newG[i] = 1;
          if (node_to_id.find({node->F, newG}) == node_to_id.end()) {
            Node *newNode = new Node{node->F, newG, nodeCounter++};
            dag[newNode->id] = newNode;
            node_to_id[{node->F, newG}] = newNode->id;
            q.push(newNode);
          }
          Node *parent_node = dag[node_to_id[{node->F, newG}]];
          node->parents.push_back(parent_node);
          parent_node->children.push_back({node, G_colors_[i]});
        }
      }
    }
    return {dag, node_to_id};
  }

  std::string MannaPnueli::simplify_color_formula(std::vector<int> F_color, std::vector<int> G_color) const {
    CUDD::BDD new_color_formula_bdd = color_formula_bdd_;
    // std::cout << new_color_formula_bdd.FactoredFormString()<< std::endl;
    for (int i = 0; i < F_color.size(); i++) {
      // retrive the i-th F color
      int color = F_colors_[i];
      // retrive the BDD var of that color
      CUDD::BDD color_bdd = color_to_variable_.at(color);
      // std::cout << color_bdd.FactoredFormString()<< std::endl;
      if (F_color[i] == 0) {
        new_color_formula_bdd = new_color_formula_bdd.Restrict(!color_bdd);
      } else {
        new_color_formula_bdd = new_color_formula_bdd.Restrict(color_bdd);
      }
    }
    // std::cout << new_color_formula_bdd.FactoredFormString()<< std::endl;
    for (int i = 0; i < G_color.size(); i++) {
      // retrive the i-th G color
      int color = G_colors_[i];
      // retrive the BDD var of that color
      CUDD::BDD color_bdd = color_to_variable_.at(color);
      if (G_color[i] == 0) {
        new_color_formula_bdd = new_color_formula_bdd.Restrict(!color_bdd);
      } else {
        new_color_formula_bdd = new_color_formula_bdd.Restrict(color_bdd);
      }
    }
    // std::cout << new_color_formula_bdd.FactoredFormString()<< std::endl;
    return color_formula_bdd_to_string(new_color_formula_bdd);
  }

  MannaPnueli::Node *MannaPnueli::bottom_node_Dag() const {
    return dag_.at(0);
  }

  MP_output_function MannaPnueli::ExtractStrategy_Explicit(MP_output_function op, int curr_node_id, CUDD::BDD gameNode,
                                                           ZielonkaNode *t,
                                                           std::vector<ELSynthesisResult> EL_results) const {
    std::cout << "-----------\ngameNode: " << gameNode;
    gameNode.PrintCover();
    std::cout << "dag node: " << curr_node_id << "\n";
    std::cout << "tree node: " << t->order << "\n";

    for (auto item: op) {
      // std::cout << item.gameNode << " " << item.t->order << "\n";
      // std::cout << item.Y << " " << item.u->order << "\n";
      if ((item.gameNode.Xnor(gameNode) == var_mgr_->cudd_mgr()->bddOne()) && (item.t->order == t->order) && (
            item.currDagNodeId == curr_node_id)) {
        std::cout << "defined! " << gameNode << " " << t->order << " " << curr_node_id << "\n";
        // gameNode.PrintCover();
        // std::cout << "stored " << item.gameNode << " " << item.t->order << "\n";
        // item.gameNode.PrintCover();
        return op;
      }
    }
    //	t: tree node, s (anchor node): lowest ancester of t that includes all colors of gameNode
    MP_output_function temp = op;
    // stop recursion if the strategy has already been defined for (gameNode,t)



    Node *curr_node = dag_.at(curr_node_id);

    std::vector<int> newFcolors = curr_node->F; // need to add 1
    std::vector<int> newGcolors = curr_node->G; // need to remove 0
    for (int i = 0; i < F_colors_.size(); i++) {
      int F_color = F_colors_[i];
      CUDD::BDD F_color_bdd = Colors_[F_color];
      if (gameNode.Restrict(F_color_bdd) != var_mgr_->cudd_mgr()->bddZero()) {
        newFcolors[i] = 1;
      }
    }

    for (int i = 0; i < G_colors_.size(); i++) {
      int G_color = G_colors_[i];
      CUDD::BDD G_color_bdd = Colors_[G_color];
      if (gameNode.Restrict(G_color_bdd) != var_mgr_->cudd_mgr()->bddOne()) {
        newGcolors[i] = 0;
      }
    }

    auto it = node_to_id_.find({newFcolors, newGcolors});
    assert(it != node_to_id_.end());
    int new_node_id = it->second;


    // CUDD::BDD newFcolors = curFcolors | (F_colors & gameNode);
    // CUDD::BDD newGcolors = curGcolors & (G_colors & gameNode);
    //
    // currentDAGnode = colorsToDAGnode(curFcolors, curGcolors);

    // ZielonkaTree *new_tree = EL_results[new_node_id].z_tree;
    // ZielonkaNode* t_prime = t;
    // if (new_node_id != curr_node_id) {
    //   t_prime = new_tree->get_root();
    // }

    CUDD::BDD winning_states = EL_results[new_node_id].winning_states;



    // the following assumes that system moves first and environment moves second

    // ZielonkaNode *s = get_anchor(gameNode, t);
    //
    // // BDD that will be used to encode a single choice for system, default bddZero
    // CUDD::BDD Y = var_mgr_->cudd_mgr()->bddZero();
    //
    // // pick one choice for system that is winning for system from gameNode for objective s
    // if (s->children.empty()) {
    //   // have just a single winningmoves BDD
    //   Y = getUniqueSystemChoice(gameNode, s->winningmoves[0]);
    //   // Y = getUniqueSystemChoice(gameNode,s->winningmoves[0]);
    // } else {
    //   // iterate through all winningmoves BDD until a choice for system is found that is winning from gameNode for objective s; one is guaranteed to be found
    //   for (int i = 0; i < s->children.size(); i++) {
    //     Y = getUniqueSystemChoice(gameNode, s->winningmoves[i]);
    //     if (Y != var_mgr_->cudd_mgr()->bddZero()) {
    //       break;
    //     }
    //   }
    // }
    //
    // // get next memory value; t: old memory value, s: anchor node, move: system choice that has been picked
    // ZielonkaNode *u = get_leaf(t, s, s, Y);

    // add system choice and resulting new memory to extracted strategy,
    // currently assumes result has component "strategy" which is vector of (gameNode, ZielonkaNode), (CUDD::BDD,ZielonkaNode)
    MPWinningMove move;
    move.gameNode = gameNode;
    move.t = t;
    move.currDagNodeId = curr_node_id;


    bool found = false;
    if (curr_node_id == new_node_id) {
      // get outputs from EL result
      for (auto item : EL_results[new_node_id].output_function) {
        if ((item.gameNode.Xnor(gameNode) == var_mgr_->cudd_mgr()->bddOne()) && (item.t->order == t->order)) {
          move.Y = item.Y;
          move.u = item.u;
          move.newDagNodeId = new_node_id;
          found = true;
          break;
        }
      }
    } else {
      for (auto item : EL_results[new_node_id].output_function) {
        if (item.gameNode.Xnor(gameNode) == var_mgr_->cudd_mgr()->bddOne()) {
          move.Y = item.Y;
          move.u = item.u;
          move.newDagNodeId = new_node_id;
          found = true;
          break;
        }
      }
    }
    assert(found);


    temp.push_back(move);
    std::cout << " --> \n";
    std::cout << "Y: " << move.Y << "\n";
    std::cout << "dag node: " << move.newDagNodeId << "\n";
    std::cout << "tree node: " << move.u->order << "\n\n";

    // compute game nodes that can result by taking system choice from gameNode
    std::vector<CUDD::BDD> newGameNodes = getSuccsWithYZ(gameNode, move.Y);

    // continue strategy construction with each possible new game node and the new memory value
    for (int i = 0; i < newGameNodes.size(); i++) {
      MP_output_function temp_new = ExtractStrategy_Explicit(temp, new_node_id, newGameNodes[i], move.u,
                                                             EL_results);

      temp = temp_new;
    }
    return temp;
  }

  std::vector<CUDD::BDD> MannaPnueli::getSuccsWithYZ(CUDD::BDD gameNode, CUDD::BDD Y) const {
    std::vector<CUDD::BDD> succs;
    std::vector<CUDD::BDD> transition_vector = spec_.transition_function();
    std::vector<CUDD::BDD> transition_vector_fix_Y_Z;
    for (int i = 0; i < transition_vector.size(); i++) {
      CUDD::BDD transition_fix_Y_Z = (transition_vector[i] * gameNode * Y).ExistAbstract(
        var_mgr_->state_variables_cube(spec_.automaton_id())).ExistAbstract(var_mgr_->output_cube());
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
        CUDD::BDD Z_var = var_mgr_->state_variable(spec_.automaton_id(), i);
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


  MPSynthesisResult MannaPnueli::run_MP() const {
    std::vector<ELSynthesisResult> EL_results(dag_.size()); //TODO here initialized as Zero just for testing
    std::vector<bool> computed(dag_.size(), false);

    while (std::find(computed.begin(), computed.end(), false) != computed.end()) {
      auto it = std::find(computed.begin(), computed.end(), false);
      int index = distance(computed.begin(), it);
      Node *node = dag_.at(index);
      // std::cout << "Now process: Dag Node " << node->id << " (";
      // for (int bit : node->F) std::cout << bit;
      // std::cout << ", ";
      // for (int bit : node->G) std::cout << bit;
      // std::cout << ")\n";


      std::string curColor_formula = simplify_color_formula(node->F, node->G);
      std::cout << curColor_formula << std::endl;

      // build Zielonka tree for current F- and G-colors
      // ZielonkaTree *Ztree = new ZielonkaTree(curColor_formula, Colors_, var_mgr_);


      CUDD::BDD instant_winning = var_mgr_->cudd_mgr()->bddZero();
      CUDD::BDD instant_losing = var_mgr_->cudd_mgr()->bddZero();
      for (auto child: node->children) {
        CUDD::BDD child_winnning_states = EL_results[child.first->id].winning_states;
        int color_flipped = child.second; // the color that got flipped
        auto is_F_color = std::find(F_colors_.begin(), F_colors_.end(), color_flipped);
        auto is_G_color = std::find(G_colors_.begin(), G_colors_.end(), color_flipped);
        assert((is_F_color != F_colors_.end()) || (is_G_color != G_colors_.end())); // it has to be either an F or G

        if (is_F_color != F_colors_.end()) {
          instant_winning = instant_winning | (child_winnning_states * Colors_[color_flipped]);
          instant_losing = instant_losing | (!child_winnning_states * Colors_[color_flipped]);
        } else {
          // instant_winning = instant_winning | (child_winnning_states * !(Colors_[color_flipped] | spec_.initial_state_bdd()));
          // instant_losing = instant_losing | (!child_winnning_states * !(Colors_[color_flipped] | spec_.initial_state_bdd()));
          instant_winning = instant_winning | (child_winnning_states * !(Colors_[color_flipped]));
          instant_losing = instant_losing | (!child_winnning_states * !(Colors_[color_flipped]));
        }
        // std::cout << "instant_winning: " << instant_winning << std::endl;
        // std::cout << "instant_losing: " << instant_losing << std::endl;
      }


      // TODO: loop over existing entries in the vector result.winning_states; each entry is a pair (colors,winningStates).
      // 		 Add nodes from winningStates for which curcolors&seencolors=colors to instantWinning
      //		 Add nodes from !winningStates for which curcolors&seencolors=colors to instantLosing
      EmersonLei solver(spec_, curColor_formula, starting_player_, protagonist_player_,
                        Colors_, var_mgr_->cudd_mgr()->bddOne(), instant_winning, instant_losing);
      ELSynthesisResult result = solver.run_EL();
      // solve EL game for curColor_formula
      //TODO change run_EL to take instantWinning, or change the constructor of EL
      // ELSynthesisResult el_synthesis_result = solver.run_EL(instantWinning, instantLosing);
      computed[index] = true;
      EL_results[index] = result;
    }
    // update result according to computed solution, TODO: store result for curcolors; also, winningmoves
    MPSynthesisResult result;

    if (EL_results[dag_.size() - 1].realizability) {
      result.realizability = true;
      result.winning_states = EL_results[dag_.size() - 1].winning_states;
      MP_output_function op;
      std::cout << "Strategy: \n";
      result.output_function = ExtractStrategy_Explicit(op, dag_.size() - 1, spec_.initial_state_bdd(),
        EL_results[dag_.size() - 1].z_tree->get_root(), EL_results);
      return result;
    } else {
      result.realizability = false;
      result.winning_states = EL_results[dag_.size() - 1].winning_states;
      return result;
    }
  }
}
