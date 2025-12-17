#ifndef SCC_DECOMPOSER_H
#define SCC_DECOMPOSER_H

#include "automata/SymbolicStateDfa.h"
#include "cuddObj.hh"

namespace Syft {

/**
 * \brief Abstract interface for SCC (Strongly Connected Component) decomposition algorithms.
 * 
 * This interface allows for different implementations of SCC decomposition algorithms
 * that can be tested independently before being used in synthesis.
 */
class SCCDecomposer {
public:
    virtual ~SCCDecomposer() = default;

    /**
     * \brief Peels off one layer of SCCs from the given state set.
     * 
     * A layer consists of all states that are in terminal SCCs (SCCs with no outgoing
     * edges to other SCCs) within the given state set.
     * 
     * \param states The set of states to peel a layer from (represented as a BDD).
     * \return A BDD representing the top layer (terminal SCCs) of the given states.
     */
    virtual CUDD::BDD PeelLayer(const CUDD::BDD& states) const = 0;
};

/**
 * \brief Chain algorithm implementation of SCC decomposition.
 * 
 * Implements the chain algorithm for computing SCC layers symbolically.
 * This algorithm is particularly efficient for weak games (obligation fragment).
 */
class ChainSCCDecomposer : public SCCDecomposer {
private:
    const SymbolicStateDfa& arena_;

public:
    /**
     * \brief Constructs a ChainSCCDecomposer from a symbolic state DFA.
     * 
     * \param arena The symbolic state DFA representing the game arena.
     */
    explicit ChainSCCDecomposer(const SymbolicStateDfa& arena)
        : arena_(arena) {}

    /**
     * \brief Peels off one layer of SCCs using the chain algorithm.
     * 
     * The chain algorithm identifies terminal SCCs by finding states s such that
     * for all states s' reachable from s, there is a path from s' back to s.
     * 
     * \param states The set of states to peel a layer from.
     * \return A BDD representing the top layer (terminal SCCs).
     */
    CUDD::BDD PeelLayer(const CUDD::BDD& states) const override;
};

/**
 * \brief Naive backward-forward algorithm implementation of SCC decomposition.
 * 
 * Implements a naive algorithm that identifies terminal SCCs by checking:
 * TopLayer(s) = States(s) & ForAll s' . (Path(s', s) -> Path(s, s'))
 * 
 * This algorithm is simpler but potentially less efficient than the chain algorithm.
 */
/**
 * \brief Result of building a relation, containing both the relation and primed variable info.
 */
struct TransitionRelationResult {
    CUDD::BDD relation;              ///< The transition relation BDD over (s, s') pairs
    std::size_t primed_automaton_id; ///< The automaton ID for the primed state variables
};

/**
 * \brief Result of building a path relation, containing both the relation and primed variable info.
 */
struct PathRelationResult {
    CUDD::BDD relation;              ///< The path relation BDD over (s, s') pairs
    std::size_t primed_automaton_id; ///< The automaton ID for the primed state variables
};

class NaiveSCCDecomposer : public SCCDecomposer {
private:
    const SymbolicStateDfa& arena_;

    /**
     * \brief Builds the one-step transition relation.
     */
    CUDD::BDD BuildTransitionRelation(std::size_t primed_automaton_id) const;

    /**
     * \brief Computes the transitive closure of a relation.
     */
    CUDD::BDD TransitiveClosure(const CUDD::BDD& relation, 
                                std::size_t primed_automaton_id,
                                std::size_t temp_automaton_id) const;

    /**
     * \brief Builds the path relation (reachability) from the transition function.
     * 
     * Path(s, s') = States(s) & States(s') & transitive closure of transitions
     * 
     * \param states The set of states to restrict the path relation to.
     * \param primed_automaton_id The automaton ID for the primed state variables.
     * \param temp_automaton_id The automaton ID for temporary variables used in closure.
     * \return A BDD representing the reachability relation over (current_state, next_state) pairs.
     */
    CUDD::BDD BuildPathRelation(const CUDD::BDD& states, 
                                std::size_t primed_automaton_id,
                                std::size_t temp_automaton_id) const;

public:
    /**
     * \brief Constructs a NaiveSCCDecomposer from a symbolic state DFA.
     * 
     * \param arena The symbolic state DFA representing the game arena.
     */
    explicit NaiveSCCDecomposer(const SymbolicStateDfa& arena)
        : arena_(arena) {}

    /**
     * \brief Peels off one layer of SCCs using the naive backward-forward algorithm.
     * 
     * The algorithm identifies terminal SCCs by finding states s such that
     * for all states s' that can reach s, there is a path from s to s'.
     * 
     * \param states The set of states to peel a layer from.
     * \return A BDD representing the top layer (terminal SCCs).
     */
    CUDD::BDD PeelLayer(const CUDD::BDD& states) const override;

    /**
     * \brief Builds the one-step transition relation and returns it with primed variable info.
     * 
     * This is exposed for testing purposes.
     * 
     * \return TransitionRelationResult containing the relation and primed automaton ID.
     */
    TransitionRelationResult BuildTransitionRelationWithPrimed() const;

    /**
     * \brief Builds the path relation (reachability) and returns it along with primed variable info.
     * 
     * This is exposed for testing purposes.
     * 
     * \param states The set of states to restrict the path relation to.
     * \return PathRelationResult containing the relation and primed automaton ID.
     */
    PathRelationResult BuildPathRelationWithPrimed(const CUDD::BDD& states) const;
};

} // namespace Syft

#endif // SCC_DECOMPOSER_H

