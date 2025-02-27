//
// Created by dh on 25/11/24.
//

#ifndef LYDIASYFT_EMERSONLEI_HPP
#define LYDIASYFT_EMERSONLEI_HPP

#include "game/DfaGameSynthesizer.h"
#include "game/ZielonkaTree.hh"

namespace Syft {
	/**
	* \brief A single-strategy-synthesizer for a Emerson-Lei game given as a symbolic-state DFA.
	*
	* Emerson-Lei condition (positive Boolean formula over colors and negated colors) holds.
	* e.g. condition 1 & !2 & (3 | 4) is satisfied by plays that visit colors 1 and (3 or 4) infinitely often
    	* and than visit color 2 finitely often
	*/
	class EmersonLei : public DfaGameSynthesizer {
		private:
		/**
		* \brief The state space to consider.
		*/
		CUDD::BDD state_space_;
		/**
		* \brief The Emerson-Lei condition represented as a Boolean formula \beta over colors
		*/
		std::vector<CUDD::BDD> Colors_;
		std::string color_formula_;
		
		public:
		
		/**
		* \brief Construct a single-strategy-synthesizer for the given Emerson-Lei game.
		*
		* \param spec A symbolic-state DFA representing the Buchi-reachability game arena.
		* \param starting_player The player that moves first each turn.
		* \param protagonist_player The player for which we aim to find the winning strategy.
		* \param Colors The Emerson-Lei condition represented as a Boolean formula \beta over colors.
		* \param state_space The state space.
		*/
		EmersonLei(const SymbolicStateDfa &spec, const std::string color_formula, Player starting_player, Player protagonist_player,
		const std::vector<CUDD::BDD> &colorBDDs, const CUDD::BDD &state_space);

    CUDD::BDD EmersonLeiSolve(ZielonkaNode *t, CUDD::BDD term) const;
    CUDD::BDD cpre(ZielonkaNode *t, int i, CUDD::BDD target) const;
		EL_output_function ExtractStrategy_Explicit(EL_output_function op, CUDD::BDD winning_states, CUDD::BDD gameNode, ZielonkaNode *t) const;
		CUDD::BDD getUniqueSystemChoice(CUDD::BDD gameNode, CUDD::BDD winningmoves) const;
		// CUDD::BDD getUniqueSystemChoice(CUDD::BDD gameNode, std::unique_ptr<Transducer> transducer) const;
		std::vector<CUDD::BDD> getSuccsWithYZ(CUDD::BDD gameNode, CUDD::BDD Y) const;
		int index_below(ZielonkaNode *anchor_node, ZielonkaNode *old_memory) const;
		ZielonkaNode* get_anchor(CUDD::BDD game_node, ZielonkaNode *memory_value) const;
		ZielonkaNode* get_leaf(ZielonkaNode *old_memory, ZielonkaNode *anchor_node, ZielonkaNode *curr, CUDD::BDD Y) const;
		inline std::vector<CUDD::BDD> transition_function() const {return spec_.transition_function();}
		inline int spec_id() const {return spec_.automaton_id();}
		SynthesisResult run() const final;
		ELSynthesisResult run_EL() const;

		
	};
}


#endif //LYDIASYFT_EMERSONLEI_HPP