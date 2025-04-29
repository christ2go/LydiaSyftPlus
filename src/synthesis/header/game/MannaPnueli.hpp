//
// Created by Shufang Zhu on 05/03/2025.
//

#ifndef MANNAPNUELI_H
#define MANNAPNUELI_H


#include "game/DfaGameSynthesizer.h"
#include "game/ZielonkaTree.hh"

namespace Syft {
	/**
	* \brief A single-strategy-synthesizer for a Manna-Pnueli game given as a symbolic-state DFA
	*
	* Manna-Pnueli condition (positive Boolean formula over colors and negated colors) holds.
	* e.g. condition 1 & !2 & (F3 | G4) is satisfied by plays that visit colors 1 infinitely often
	* and visit color 3 once
	* and visit color 4 all the time
  * and visit color 2 finitely often
	*/
	class MannaPnueli : public DfaGameSynthesizer {
		private:
		/**
		* \brief The state space to consider.
		*/
		CUDD::BDD state_space_;
		std::vector<CUDD::BDD> Colors_;

		//The following are defined for managing the color_formula
		std::vector<int> F_colors_;
		std::vector<int> G_colors_;
		CUDD::Cudd color_mgr_;
		std::map<int, CUDD::BDD> color_to_variable_;
		std::map<int, int> bdd_id_to_color_;
		/**
		* \brief The Manna-Pnueli condition represented as a Boolean formula \beta over colors
		*/
		std::string color_formula_;
		/**
		* \brief The Manna-Pnueli condition represented as a BDD
		*/
    CUDD::BDD color_formula_bdd_;
		struct Node {
			std::vector<int> F;
			std::vector<int> G;
			int id;
			std::vector<Node*> parents; // Store parent nodes directly
			std::vector<std::pair<Node*, int>> children; // Store child nodes directly
		};

		// Custom hash function for unordered_map
		struct VectorHash {
			size_t operator()(const std::pair<std::vector<int>, std::vector<int>>& p) const {
				size_t hashF = 0, hashG = 0;
				for (int i : p.first) hashF = hashF * 31 + i;
				for (int i : p.second) hashG = hashG * 31 + i;
				return hashF ^ hashG;
			}
		};
		typedef std::unordered_map<int, Node*> Dag; // Store nodes by their unique ID
		typedef std::unordered_map<std::pair<std::vector<int>, std::vector<int>>, int, VectorHash> Node_to_Id;
		Dag dag_; // The bottom node is having all F-bits as 1 and G-bits as 0, the id of this node is 0
		Node_to_Id node_to_id_;

		std::pair<Dag, Node_to_Id> build_FG_dag();
		std::string simplify_color_formula(std::vector<int> F_color, std::vector<int> G_color) const;
		std::string color_formula_bdd_to_string (const CUDD::BDD &color_formula_bdd) const;
		void print_FG_dag() const;
		std::string remove_whitespace(const std::string &str) const;
		int precedence(char op) const;
		std::string infix_to_postfix(const std::string &infix) const;
		Node* bottom_node_Dag() const;
		std::vector<CUDD::BDD> getSuccsWithYZ(CUDD::BDD gameNode, CUDD::BDD Y) const;

		void MP_solve();

		public:

		/**
		* \brief Construct a single-strategy-synthesizer for the given Manna-Pnueli game.
		*
		* \param spec A symbolic-state DFA representing the Manna-Pnueli game arena.
		* \param starting_player The player that moves first each turn.
		* \param protagonist_player The player for which we aim to find the winning strategy.
		* \param Colors The Manna-Pnueli condition represented as a Boolean formula \beta over colors.
		* \param state_space The state space.
		*/
		MannaPnueli(const SymbolicStateDfa &spec, std::string color_formula, std::vector<int> F_colors_,
		std::vector<int> G_colors_, Player starting_player, Player protagonist_player,
		const std::vector<CUDD::BDD> &colorBDDs, const CUDD::BDD &state_space);

		CUDD::BDD boolean_string_to_bdd(const std::string &color_formula);

		MP_output_function ExtractStrategy_Explicit(MP_output_function op, int curr_node_id, CUDD::BDD gameNode,
																													ZielonkaNode *t,
																													std::vector<ELSynthesisResult> EL_results) const;

		MPSynthesisResult run_MP() const;

		// We need to keep it cause MannaPnueli is inherited from DfaGameSynthesizer
		SynthesisResult run() const final;


	};
}



#endif //MANNAPNUELI_H
