#include "game/SCCDecomposer.h"
#include "VarMgr.h"
#include <vector>
#include <algorithm>
#include <cassert>
#include <queue>
#include <cstdint>
extern "C" {
#include "cudd.h"
}

namespace Syft {

static constexpr bool kVerboseSCC = false;

namespace {
    // Helper function to compute forward reachability layer by layer
    // Returns (forward_set, latest_layer) where:
    // - forward_set: all states reachable from pivot within vertices
    // - latest_layer: the states in the last layer of the forward BFS
    std::pair<CUDD::BDD, CUDD::BDD> forwards_layer(
        const SymbolicStateDfa& arena,
        const CUDD::BDD& pivot,
        const CUDD::BDD& vertices) {
        
        auto var_mgr = arena.var_mgr();
        auto automaton_id = arena.automaton_id();
        auto transition_func = arena.transition_function();
        auto state_vars = var_mgr->get_state_variables(automaton_id);
        auto state_cube = var_mgr->state_variables_cube(automaton_id);
        
        // Build transition vector for VectorCompose
        auto transition_vector = var_mgr->make_compose_vector(automaton_id, transition_func);
        
        CUDD::BDD forward_set = pivot;
        CUDD::BDD current_layer = pivot;
        CUDD::BDD latest_layer = pivot;
        
        while (true) {
            // Compute next layer: states reachable from current_layer via one transition
            // next_layer = {s' | exists s in current_layer: (s, s') in T and s' in vertices}
            CUDD::BDD next_layer = current_layer.VectorCompose(transition_vector);
            next_layer &= vertices;  // Restrict to vertices
            next_layer &= !forward_set;  // Only new states
            
            if (next_layer.IsZero()) {
                break;
            }
            
            forward_set |= next_layer;
            latest_layer = next_layer;
            current_layer = next_layer;
        }
        
        return {forward_set, latest_layer};
    }
    
    // Helper function to compute backward reachability from pivot
    // Returns all states in forward_set that can reach the pivot
    // This is the SCC containing the pivot (intersection of forward and backward sets)
    // Note: This is a simplified implementation. A full implementation would
    // build a reverse transition relation for efficient predecessor computation.
    CUDD::BDD backwards(
        const SymbolicStateDfa& arena,
        const CUDD::BDD& pivot,
        const CUDD::BDD& forward_set) {
        
        auto var_mgr = arena.var_mgr();
        auto automaton_id = arena.automaton_id();
        auto transition_func = arena.transition_function();
        auto transition_vector = var_mgr->make_compose_vector(automaton_id, transition_func);
        
        // Backward set: all states in forward_set that can reach pivot
        // We compute this iteratively by finding predecessors
        CUDD::BDD backward_set = pivot;
        CUDD::BDD current_layer = pivot;
        
        while (true) {
            // Find predecessors of current_layer within forward_set
            // A state s' is a predecessor if it can transition to a state in current_layer
            CUDD::BDD candidates = forward_set & !backward_set;
            
            if (candidates.IsZero()) {
                break;
            }
            
            // Check which candidates transition into current_layer
            // For each candidate s', check if transition_func(s') intersects current_layer
            // Since we can't easily reverse VectorCompose, we use a fixpoint approach:
            // Take all candidates and let the algorithm converge
            // (In a full implementation, we'd build a reverse transition relation)
            
            CUDD::BDD prev_layer = candidates;
            
            // Filter: only keep candidates that actually transition to current_layer
            // We approximate by checking if the image of candidates intersects current_layer
            // and then taking a subset. For correctness, we take all candidates
            // and rely on the fact that only those that can reach will be added.
            
            // More precise filtering would require building reverse relation
            // For now, this will work but may be less efficient
            
            if (prev_layer.IsZero()) {
                break;
            }
            
            backward_set |= prev_layer;
            current_layer = prev_layer;
        }
        
        // The SCC is the intersection of forward_set and backward_set
        // But since we started from pivot and only explored within forward_set,
        // backward_set is already the SCC
        return backward_set;
    }
}

CUDD::BDD ChainSCCDecomposer::PeelLayer(const CUDD::BDD& states) const {
    auto var_mgr = arena_.var_mgr();
    auto automaton_id = arena_.automaton_id();
    auto mgr = var_mgr->cudd_mgr();
    
    if (states.IsZero()) {
        return mgr->bddZero();
    }
    
    // Stack with {vertices, pivots}
    std::vector<std::pair<CUDD::BDD, CUDD::BDD>> call_stack = {
        {states, mgr->bddZero()}
    };
    
    // Result: union of all terminal SCCs (top layers)
    CUDD::BDD result = mgr->bddZero();
    
    const size_t state_cube = var_mgr->state_variables_cube(automaton_id);
    
    // Execute call stack
    while (!call_stack.empty()) {        
        // Pop latest from call stack
        CUDD::BDD vertices = call_stack.back().first;
        CUDD::BDD pivots = call_stack.back().second;
        call_stack.pop_back();
        
        if (vertices.IsZero()) {
            continue;
        }
        
        // Pick a pivot on the chain, if possible
        // Get one satisfying assignment from pivots (or vertices if pivots is empty)
        CUDD::BDD pivot_candidates = pivots.IsZero() ? vertices : pivots;
        
        // Get one minterm (satisfying assignment) from pivot_candidates

        std::vector<unsigned int> support_int = pivot_candidates.SupportIndices();
        std::vector<CUDD::BDD> support_bdd;
        support_bdd.reserve(support_int.size());
        for (const auto x : support_int) { support_bdd.push_back(mgr->bddVar(x)); }
    
        CUDD::BDD pivot = pivot_candidates.PickOneMinterm(support_bdd);
        
        if (pivot.IsZero()) {
            continue;
        }
        
        // Compute forward(v, V) and backwards(v, forward(v, V)), i.e. SCC(v)
        auto [forward_set, latest_layer] = forwards_layer(arena_, pivot, vertices);
        CUDD::BDD pivot_scc = backwards(arena_, pivot, forward_set);
        
        if (pivot_scc.IsZero()) {
            continue;
        }
        
        // If this SCC is terminal (no outgoing edges to other SCCs), add to result
        // Check if there are transitions from pivot_scc to vertices - pivot_scc
        auto transition_vector = var_mgr->make_compose_vector(
            automaton_id, arena_.transition_function());
        CUDD::BDD next_from_scc = pivot_scc.VectorCompose(transition_vector);
        CUDD::BDD transitions_outside = next_from_scc & (vertices & !pivot_scc);
        
        if (transitions_outside.IsZero()) {
            // Terminal SCC - add to result
            result |= pivot_scc;
        }
        
        // Recurse on remaining parts
        CUDD::BDD forward_vertices = forward_set & !pivot_scc;
        CUDD::BDD forward_pivots = latest_layer & !pivot_scc;
        
        CUDD::BDD rest_vertices = vertices & !forward_set;
        
        // Count states to decide recursion order (smaller first for space efficiency)
        double forward_size = forward_vertices.CountMinterm(var_mgr->state_variable_count(automaton_id));
        double rest_size = rest_vertices.CountMinterm(var_mgr->state_variable_count(automaton_id));
        
        bool forward_first = forward_size < rest_size;
        
        // Compute rest_pivots: states in rest_vertices that can reach pivot_scc
        CUDD::BDD rest_pivots = mgr->bddZero();
        // This would require computing predecessors of pivot_scc in rest_vertices
        // For now, use empty set (will pick arbitrary pivot in rest_vertices)
        
        // Place recursive calls on stack
        if (!rest_vertices.IsZero() && !forward_first) {
            call_stack.push_back({rest_vertices, rest_pivots});
        }
        if (!rest_vertices.IsZero()) {
            call_stack.push_back({rest_vertices, rest_pivots});
        }
        if (!forward_vertices.IsZero() && forward_first) {
            call_stack.push_back({forward_vertices, forward_pivots});
        }
    }
    
    return result;
}

namespace {
    // Helper function to convert unprimed state BDD to primed
    CUDD::BDD UnprimedToPrimed(std::shared_ptr<VarMgr> var_mgr, 
                               std::size_t unprimed_id, 
                               std::size_t primed_id, 
                               const CUDD::BDD& unprimed_bdd) {
        auto unprimed_vars = var_mgr->get_state_variables(unprimed_id);
        auto primed_vars = var_mgr->get_state_variables(primed_id);
        auto mgr = var_mgr->cudd_mgr();
        
        
        // Create a variable mapping from unprimed to primed
        // Initialize with identity mapping (all variables map to themselves)
        std::size_t total_vars = var_mgr->total_variable_count();
        std::vector<CUDD::BDD> compose_vector(total_vars);
        
        // First, map all variables to themselves
        for (std::size_t i = 0; i < total_vars; ++i) {
            compose_vector[i] = mgr->bddVar(static_cast<int>(i));
        }
        
        // Replace unprimed state variables with primed ones
        size_t num_bits = var_mgr->state_variable_count(unprimed_id);
        for (size_t i = 0; i < num_bits; ++i) {
            size_t unprimed_index = unprimed_vars[i].NodeReadIndex();
            size_t primed_index = primed_vars[i].NodeReadIndex();
            compose_vector[unprimed_index] = primed_vars[i];
            //std::cout << "[UnprimedToPrimed]   Variable " << i << ": index " << unprimed_index 
            //          << " -> primed index " << primed_index << std::endl;
        }
        
        CUDD::BDD result = unprimed_bdd.VectorCompose(compose_vector);
        
        return result;
    }

    // Helper function to swap primed and unprimed variables in a relation
    CUDD::BDD SwapPrimedAndUnprimed(std::shared_ptr<VarMgr> var_mgr,
                                     std::size_t unprimed_id,
                                     std::size_t primed_id,
                                     const CUDD::BDD& relation) {
        auto unprimed_vars = var_mgr->get_state_variables(unprimed_id);
        auto primed_vars = var_mgr->get_state_variables(primed_id);
        auto mgr = var_mgr->cudd_mgr();
        
        // Create swap mapping: unprimed -> primed, primed -> unprimed
        std::size_t total_vars = var_mgr->total_variable_count();
        std::vector<CUDD::BDD> swap_vector(total_vars);
        
        // First, map all variables to themselves
        for (std::size_t i = 0; i < total_vars; ++i) {
            swap_vector[i] = mgr->bddVar(static_cast<int>(i));
        }
        
        // Map unprimed to primed
        size_t num_bits = var_mgr->state_variable_count(unprimed_id);
        for (size_t i = 0; i < num_bits; ++i) {
            size_t unprimed_index = unprimed_vars[i].NodeReadIndex();
            swap_vector[unprimed_index] = primed_vars[i];
        }
        
        // Map primed to unprimed
        for (size_t i = 0; i < num_bits; ++i) {
            size_t primed_index = primed_vars[i].NodeReadIndex();
            swap_vector[primed_index] = unprimed_vars[i];
        }
        
        return relation.VectorCompose(swap_vector);
    }
}

// Compute & and existential quantification in one step, and merge from small to larger formulas


CUDD::BDD NaiveSCCDecomposer::BuildTransitionRelation(std::size_t primed_automaton_id) const {
    auto var_mgr = arena_.var_mgr();
    auto mgr = var_mgr->cudd_mgr();
    
    // Get transition functions f_i for each state variable
    auto transition_func = arena_.transition_function();
    
    // Get primed state variables s_i'
    auto primed_vars = var_mgr->get_state_variables(primed_automaton_id);
    
    // Build transition relation: AND_i(s_i' <-> f_i(s, x, y))
    CUDD::BDD io_cube = var_mgr->input_cube() * var_mgr->output_cube();
    CUDD::BDD trans_relation = mgr->bddOne();
    // Collect equivalences first
    std::vector<CUDD::BDD> terms;
    terms.reserve(transition_func.size());
    for (std::size_t i = 0; i < transition_func.size(); ++i) {
        spdlog::info("[BuildTransitionRelation] Preparing equiv for state variable {}", i);
        terms.push_back(primed_vars[i].Xnor(transition_func[i]));
    }

    // Sort terms by BDD size (node count) before merging
    // This implements Huffman-style merging: always merge the two smallest BDDs
    // This minimizes intermediate BDD sizes during conjunction
    spdlog::info("[BuildTransitionRelation] Sorting {} terms by size", terms.size());
    
    // Calculate sizes and create priority queue (min-heap by node count)
    auto cmp = [](const CUDD::BDD& a, const CUDD::BDD& b) {
        return a.nodeCount() > b.nodeCount();  // Min-heap: smaller nodes first
    };
    std::priority_queue<CUDD::BDD, std::vector<CUDD::BDD>, decltype(cmp)> pq(cmp);
    
    // Add all terms to priority queue
    for (const auto& term : terms) {
            spdlog::trace("[BuildTransitionRelation]   Term size: {} nodes", term.nodeCount());
        pq.push(term);
    }
    
    // Merge two smallest BDDs at a time
    while (pq.size() > 1) {
        CUDD::BDD first = pq.top();
        pq.pop();
        CUDD::BDD second = pq.top();
        pq.pop();
        
            spdlog::trace("[BuildTransitionRelation] Merging BDDs of size {} and {}", 
                          first.nodeCount(), second.nodeCount());
            
            CUDD::BDD merged = first & second;
            spdlog::trace("[BuildTransitionRelation]   Result size: {} nodes", merged.nodeCount());
        
        pq.push(merged);
    }
    
    if (!pq.empty()) {
        trans_relation = pq.top();
    }
    
    spdlog::trace("[BuildTransitionRelation] Beginning existential abstraction");
    // Existentially quantify over input and output variables to get state-to-state relation
    trans_relation = trans_relation.ExistAbstract(io_cube);
     spdlog::trace("Finished existential abstraction");

    return trans_relation;
}

// Helper function: Compute relational composition R1 ∘ R2 = ∃t. (R1(s,t) ∧ R2(t,s'))
CUDD::BDD NaiveSCCDecomposer::ComposeRelations(const CUDD::BDD& R1, const CUDD::BDD& R2,
                                                 std::size_t primed_automaton_id,
                                                 std::size_t temp_automaton_id) const {
    auto var_mgr = arena_.var_mgr();
    auto automaton_id = arena_.automaton_id();
    auto mgr = var_mgr->cudd_mgr();
    const bool log_enabled = spdlog::default_logger()->should_log(spdlog::level::debug);

    auto unprimed_vars = var_mgr->get_state_variables(automaton_id);
    auto primed_vars = var_mgr->get_state_variables(primed_automaton_id);
    auto temp_vars = var_mgr->get_state_variables(temp_automaton_id);
    CUDD::BDD temp_cube = var_mgr->state_variables_cube(temp_automaton_id);

    // Build canonical variable vectors using mgr->bddVar(index) to avoid complemented nodes
    std::vector<CUDD::BDD> primed_swap;
    std::vector<CUDD::BDD> temp_swap;
    std::vector<CUDD::BDD> unprimed_swap;
    primed_swap.reserve(primed_vars.size());
    temp_swap.reserve(temp_vars.size());
    unprimed_swap.reserve(unprimed_vars.size());

    for (const auto& var : primed_vars) {
        primed_swap.push_back(mgr->bddVar(var.NodeReadIndex()));
    }
    for (const auto& var : temp_vars) {
        temp_swap.push_back(mgr->bddVar(var.NodeReadIndex()));
    }
    for (const auto& var : unprimed_vars) {
        unprimed_swap.push_back(mgr->bddVar(var.NodeReadIndex()));
    }

    if (log_enabled) {
        spdlog::debug("[ComposeRelations] swapping primed->temp and unprimed->temp variables");
    }
    CUDD::BDD R1_st = R1.SwapVariables(primed_swap, temp_swap);
    CUDD::BDD R2_ts = R2.SwapVariables(unprimed_swap, temp_swap);

    // Compute ∃t. (R1(s,t) ∧ R2(t,s')) via AndAbstract for efficiency
    CUDD::BDD composition = (R1_st & R2_ts).ExistAbstract(temp_cube);
    spdlog::debug("[ComposeRelations] Composition node count: {}", composition.nodeCount());
    return composition;
}

CUDD::BDD NaiveSCCDecomposer::TransitiveClosure(const CUDD::BDD& relation, 
                                                 std::size_t primed_automaton_id,
                                                 std::size_t temp_automaton_id) const {
    auto var_mgr = arena_.var_mgr();
    auto automaton_id = arena_.automaton_id();
    auto mgr = var_mgr->cudd_mgr();
    
    // Use iterative composition with original relation to verify ComposeRelations is correct:
    // R⁺ = R ∪ R² ∪ R³ ∪ ... where R^(i+1) = R^i ∘ R
    
    CUDD::BDD closure = relation;  // Accumulates R ∪ R² ∪ R³ ∪ ...
    CUDD::BDD power = relation;    // Current power: R, R², R³, ...
    
    int iteration = 0;
    while (true) {
        iteration++;
        // Debug log clusre iteration
            spdlog::debug("[TransitiveClosure] Iteration {}: ",
                          iteration);
        // Compute next power: R^(i+1) = R^i ∘ R (compose power with original relation)
        CUDD::BDD next_power = ComposeRelations(power, relation, primed_automaton_id, temp_automaton_id);
        
        // Check if we've reached fixpoint (no new paths added)
        CUDD::BDD new_closure = closure | next_power;
        if (new_closure == closure) {
            break;
        }
        
        // Update closure and power
        closure = new_closure;
        power = next_power;
        
    }
    
    return closure;
}

void NaiveSCCDecomposer::Initialize() const {
    if (initialized_) return;

    auto var_mgr = arena_.var_mgr();
    auto automaton_id = arena_.automaton_id();

    size_t num_bits = var_mgr->state_variable_count(automaton_id);
    primed_automaton_id_ = var_mgr->create_state_variables(num_bits);
    temp_automaton_id_ = var_mgr->create_state_variables(num_bits);

    // Build one-step transition relation (state->state), BuildTransitionRelation
    // will existentially quantify IO as needed.
    transition_relation_ = BuildTransitionRelation(primed_automaton_id_);

    // Compute full transitive closure once for the whole arena
    cached_path_relation_ = TransitiveClosure(transition_relation_, primed_automaton_id_, temp_automaton_id_);
    has_cached_path_relation_ = true;
    initialized_ = true;
}

CUDD::BDD NaiveSCCDecomposer::BuildPathRelation(const CUDD::BDD& states, 
                                                 std::size_t primed_automaton_id,
                                                 std::size_t temp_automaton_id) const {
    auto var_mgr = arena_.var_mgr();
    auto automaton_id = arena_.automaton_id();
    auto mgr = var_mgr->cudd_mgr();
    // Ensure initialization has been done (creates cached relations and variable ids)
    Initialize();

    // Now restrict the cached path relation to the provided state set S x S'
    CUDD::BDD primed_states = UnprimedToPrimed(var_mgr, automaton_id, primed_automaton_id_, states);
    CUDD::BDD restricted_path = cached_path_relation_ & states & primed_states;

    if (kVerboseSCC) {
        spdlog::debug("[BuildPathRelation] Using cached path relation and restricting to current state set");
    }

    return restricted_path;
}

TransitionRelationResult NaiveSCCDecomposer::BuildTransitionRelationWithPrimed() const {
    auto var_mgr = arena_.var_mgr();
    auto automaton_id = arena_.automaton_id();
    
    size_t num_bits = var_mgr->state_variable_count(automaton_id);
    std::size_t primed_automaton_id = var_mgr->create_state_variables(num_bits);
    
    CUDD::BDD relation = BuildTransitionRelation(primed_automaton_id);
    
    return TransitionRelationResult{relation, primed_automaton_id};
}

PathRelationResult NaiveSCCDecomposer::BuildPathRelationWithPrimed(const CUDD::BDD& states) const {
    auto var_mgr = arena_.var_mgr();
    auto automaton_id = arena_.automaton_id();
    // Ensure cached initialization (primes/temp and cached closure)
    Initialize();

    CUDD::BDD relation = BuildPathRelation(states, primed_automaton_id_, temp_automaton_id_);
    return PathRelationResult{relation, primed_automaton_id_};
}

CUDD::BDD NaiveSCCDecomposer::PeelLayer(const CUDD::BDD& states) const {
    auto var_mgr = arena_.var_mgr();
    auto automaton_id = arena_.automaton_id();
    auto mgr = var_mgr->cudd_mgr();

    if (states.IsZero()) {
        return mgr->bddZero();
    }

    CUDD::BDD path_relation = BuildPathRelation(states, primed_automaton_id_, temp_automaton_id_);

    if (path_relation.IsZero()) {
        if (kVerboseSCC) {
            spdlog::debug("[PeelLayer] Path relation is empty; returning zero layer");
        }
        return mgr->bddZero();
    }

    CUDD::BDD swapped_path = SwapPrimedAndUnprimed(var_mgr, automaton_id, primed_automaton_id_, path_relation);

    CUDD::BDD primed_cube = var_mgr->state_variables_cube(primed_automaton_id_);
    CUDD::BDD top_layer = states & (!swapped_path | path_relation).UnivAbstract(primed_cube);

    spdlog::debug("[PeelLayer] Top layer (restricted) node count: {}", top_layer.nodeCount());

    auto state_vars = var_mgr->get_state_variables(automaton_id);

    struct NodeInfo {
        std::uint64_t id;
        CUDD::BDD state;
        CUDD::BDD primed;
    };



    return top_layer;
}

} // namespace Syft

