#ifndef WEAK_GAME_SOLVER_H
#define WEAK_GAME_SOLVER_H

#include "automata/SymbolicStateDfa.h"
#include "game/SCCDecomposer.h"
#include "VarMgr.h"
#include "cuddObj.hh"
#include <memory>
#include <vector>

namespace Syft {

/**
 * \brief Result of the weak game solver containing winning states and moves.
 */
struct WeakGameResult {
    CUDD::BDD winning_states;  ///< States from which the protagonist can win
    CUDD::BDD winning_moves;   ///< Winning moves for the protagonist
};

/**
 * \brief Solver for weak parity games using SCC decomposition.
 * 
 * Implements the algorithm:
 * 1. Compute SCC decomposition, mark each SCC as accepting (⊆ F) or rejecting (⊆ V\F)
 * 2. For bottom SCCs: 
 *    - Accepting: compute W = νX. (S\L) ∩ CPre_s(X)
 *    - Rejecting: compute W = μX. W ∪ CPre_s(X)
 * 3. Add W_S ∪ CPre_s(W_S) to W; add L_S ∪ CPre_e(L_S) to L
 * 4. Remove bottom SCCs and repeat
 */
class WeakGameSolver {
private:
    const SymbolicStateDfa& arena_;
    std::shared_ptr<VarMgr> var_mgr_;
    CUDD::BDD accepting_states_;  ///< F - the accepting/final states
    std::unique_ptr<SCCDecomposer> decomposer_;
    
    // Cached primed automaton ID for transition computations
    mutable std::size_t primed_automaton_id_;
    mutable bool initialized_ = false;
    
    bool debug_ = true;  ///< Enable debug printing of state sets
    
    /**
     * \brief Print the actual states in a BDD for debugging.
     */
    void PrintStateSet(const std::string& name, const CUDD::BDD& states) const;
    
    /**
     * \brief Initialize cached variables.
     */
    void Initialize() const;
    
    /**
     * \brief Controllable predecessor for system (protagonist).
     * CPre_s(X) = {s | ∃y. ∀x'. T(s,y,x') → ∃y'. X(x',y')}
     * States from which protagonist can force reaching X in one step.
     */
    CUDD::BDD CPreSystem(const CUDD::BDD& target, const CUDD::BDD& state_space) const;
    
    /**
     * \brief Controllable predecessor for environment (antagonist).
     * CPre_e(X) = {s | ∀y. ∃x'. T(s,y,x') ∧ ∀y'. X(x',y')}
     * States from which environment can force reaching X in one step.
     */
    CUDD::BDD CPreEnvironment(const CUDD::BDD& target, const CUDD::BDD& state_space) const;
    
    /**
     * \brief Solve reachability game to goal_states within state_space.
     * Compute μX. goal ∪ CPre_s(X)
     */
    CUDD::BDD SolveReachability(const CUDD::BDD& goal_states, const CUDD::BDD& state_space) const;
    
    /**
     * \brief Solve safety game staying in safe_states within state_space.
     * Compute νX. safe ∩ CPre_s(X)
     */
    CUDD::BDD SolveSafety(const CUDD::BDD& safe_states, const CUDD::BDD& state_space) const;
    
    /**
     * \brief Dump DFA transitions and accepting states for debugging.
     */
    void DumpDFA() const;
    
    /**
     * \brief Dump DFA in machine-readable format for Python reconstruction.
     */
    void DumpDFAForPython() const;

public:
    /**
     * \brief Constructs a WeakGameSolver.
     * 
     * \param arena The symbolic state DFA representing the game arena.
     * \param accepting_states The set of accepting/final states (F).
     */
    WeakGameSolver(const SymbolicStateDfa& arena, const CUDD::BDD& accepting_states, bool debug = false);
    
    /**
     * \brief Solve the weak parity game.
     * 
     * \return WeakGameResult containing winning states and moves.
     */
    WeakGameResult Solve() const;
    
    /**
     * \brief Check if the initial state is winning.
     */
    bool IsWinning() const;
};

} // namespace Syft

#endif // WEAK_GAME_SOLVER_H

