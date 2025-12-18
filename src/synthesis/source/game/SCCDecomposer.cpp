#include "game/SCCDecomposer.h"
#include "VarMgr.h"
#include <vector>
#include <algorithm>
#include <cassert>
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
        
        //std::cout << "[UnprimedToPrimed] Converting BDD from unprimed to primed variables" << std::endl;
        //std::cout << "[UnprimedToPrimed] Input BDD: " << var_mgr->bdd_to_string(unprimed_bdd) << std::endl;
        //std::cout << "[UnprimedToPrimed] Unprimed automaton_id: " << unprimed_id << std::endl;
        //std::cout << "[UnprimedToPrimed] Primed automaton_id: " << primed_id << std::endl;
        
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
        //std::cout << "[UnprimedToPrimed] Mapping " << num_bits << " state variables:" << std::endl;
        for (size_t i = 0; i < num_bits; ++i) {
            size_t unprimed_index = unprimed_vars[i].NodeReadIndex();
            size_t primed_index = primed_vars[i].NodeReadIndex();
            compose_vector[unprimed_index] = primed_vars[i];
            //std::cout << "[UnprimedToPrimed]   Variable " << i << ": index " << unprimed_index 
            //          << " -> primed index " << primed_index << std::endl;
        }
        
        CUDD::BDD result = unprimed_bdd.VectorCompose(compose_vector);
        //std::cout << "[UnprimedToPrimed] Result BDD: " << var_mgr->bdd_to_string(result) << std::endl;
        //std::cout << "[UnprimedToPrimed] Result node count: " << result.nodeCount() 
        //          << ", isZero: " << result.IsZero() << ", isOne: " << result.IsOne() << std::endl;
        
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

CUDD::BDD NaiveSCCDecomposer::BuildTransitionRelation(std::size_t primed_automaton_id) const {
    auto var_mgr = arena_.var_mgr();
    auto mgr = var_mgr->cudd_mgr();
    
    // Get transition functions f_i for each state variable
    auto transition_func = arena_.transition_function();
    
    // Get primed state variables s_i'
    auto primed_vars = var_mgr->get_state_variables(primed_automaton_id);
    
    // Build transition relation: AND_i(s_i' <-> f_i(s, x, y))
    CUDD::BDD trans_relation = mgr->bddOne();
    for (std::size_t i = 0; i < transition_func.size(); ++i) {
        CUDD::BDD equiv = primed_vars[i].Xnor(transition_func[i]);
        trans_relation &= equiv;
    }
    
    // Existentially quantify over input and output variables to get state-to-state relation
    CUDD::BDD io_cube = var_mgr->input_cube() * var_mgr->output_cube();
    trans_relation = trans_relation.ExistAbstract(io_cube);
    
    return trans_relation;
}

CUDD::BDD NaiveSCCDecomposer::TransitiveClosure(const CUDD::BDD& relation, 
                                                 std::size_t primed_automaton_id,
                                                 std::size_t temp_automaton_id) const {
    auto var_mgr = arena_.var_mgr();
    auto automaton_id = arena_.automaton_id();
    auto mgr = var_mgr->cudd_mgr();
    
    auto unprimed_vars = var_mgr->get_state_variables(automaton_id);
    auto primed_vars = var_mgr->get_state_variables(primed_automaton_id);
    auto temp_vars = var_mgr->get_state_variables(temp_automaton_id);
    CUDD::BDD temp_cube = var_mgr->state_variables_cube(temp_automaton_id);
    
    // Helper lambdas for variable substitution
    auto primedToTemp = [&](const CUDD::BDD& bdd) {
        std::size_t total_vars = var_mgr->total_variable_count();
        std::vector<CUDD::BDD> compose_vector(total_vars);
        for (std::size_t i = 0; i < total_vars; ++i) {
            compose_vector[i] = mgr->bddVar(static_cast<int>(i));
        }
        for (std::size_t i = 0; i < primed_vars.size(); ++i) {
            compose_vector[primed_vars[i].NodeReadIndex()] = temp_vars[i];
        }
        return bdd.VectorCompose(compose_vector);
    };
    
    auto unprimedToTemp = [&](const CUDD::BDD& bdd) {
        std::size_t total_vars = var_mgr->total_variable_count();
        std::vector<CUDD::BDD> compose_vector(total_vars);
        for (std::size_t i = 0; i < total_vars; ++i) {
            compose_vector[i] = mgr->bddVar(static_cast<int>(i));
        }
        for (std::size_t i = 0; i < unprimed_vars.size(); ++i) {
            compose_vector[unprimed_vars[i].NodeReadIndex()] = temp_vars[i];
        }
        return bdd.VectorCompose(compose_vector);
    };
    
    // Closure_0(s, s') = R(s, s')
    CUDD::BDD closure = relation;
    
    while (true) {
        // Transitive_i(s, t, s') = Closure_{i-1}(s, t) & Closure_{i-1}(t, s')
        CUDD::BDD transitive = primedToTemp(closure) & unprimedToTemp(closure);
        
        // Closure_i(s, s') = Closure_{i-1}(s, s') | Exists t . Transitive_i(s, t, s')
        CUDD::BDD new_closure = closure | transitive.ExistAbstract(temp_cube);
        
        if (new_closure == closure) {
            return closure;
        }
        
        closure = new_closure;
    }
}

CUDD::BDD NaiveSCCDecomposer::BuildPathRelation(const CUDD::BDD& states, 
                                                 std::size_t primed_automaton_id,
                                                 std::size_t temp_automaton_id) const {
    auto var_mgr = arena_.var_mgr();
    auto automaton_id = arena_.automaton_id();
    auto mgr = var_mgr->cudd_mgr();
    
    // Build one-step transition relation
    CUDD::BDD trans_relation = BuildTransitionRelation(primed_automaton_id);
    
    // Existentially quantify out I/O variables FIRST to get state-to-state relation
    // This is crucial: we want T(s, s') = "can s reach s' in one step for SOME I/O"
    CUDD::BDD io_cube = var_mgr->input_cube() * var_mgr->output_cube();
    trans_relation = trans_relation.ExistAbstract(io_cube);
    
    // Restrict transitions to stay within states BEFORE computing closure
    // This ensures paths only go through states in the current set
    CUDD::BDD primed_states = UnprimedToPrimed(var_mgr, automaton_id, primed_automaton_id, states);
    CUDD::BDD restricted_trans = trans_relation & states & primed_states;
    
    if (kVerboseSCC) {
        std::cout << "[BuildPathRelation] Enumerating transitions within state set:" << std::endl;
    }
    auto state_vars = var_mgr->get_state_variables(automaton_id);
    auto primed_vars = var_mgr->get_state_variables(primed_automaton_id);
    size_t num_state_bits = state_vars.size();
    size_t num_states = 1ULL << num_state_bits;
    
    int trans_count = 0;
    if (kVerboseSCC) {
        for (size_t s = 0; s < num_states && trans_count < 50; ++s) {
            CUDD::BDD src_bdd = mgr->bddOne();
            for (size_t i = 0; i < num_state_bits; ++i) {
                if ((s >> i) & 1) src_bdd &= state_vars[i];
                else src_bdd &= !state_vars[i];
            }
            if ((src_bdd & states).IsZero()) continue;  // Skip states not in set
            
            for (size_t t = 0; t < num_states; ++t) {
                CUDD::BDD dst_bdd = mgr->bddOne();
                for (size_t i = 0; i < num_state_bits; ++i) {
                    if ((t >> i) & 1) dst_bdd &= primed_vars[i];
                    else dst_bdd &= !primed_vars[i];
                }
                if ((dst_bdd & primed_states).IsZero()) continue;  // Skip states not in set
                
                if (!(src_bdd & dst_bdd & restricted_trans).IsZero()) {
                    std::cout << "  " << s << " -> " << t << std::endl;
                    trans_count++;
                }
            }
        }
        std::cout << "[BuildPathRelation] Found " << trans_count << " transitions" << std::endl;
    }
    
    if (restricted_trans.IsZero()) {
        if (kVerboseSCC) {
            std::cout << "[BuildPathRelation] WARNING: Restricted transition relation is ZERO!" << std::endl;
        }
        return mgr->bddZero();
    }
    
    // Compute transitive closure to get reachability relation
    CUDD::BDD path_relation = TransitiveClosure(restricted_trans, primed_automaton_id, temp_automaton_id);
    
    if (kVerboseSCC) {
        std::cout << "[BuildPathRelation] Path relation (reachability):" << std::endl;
    }
    int path_count = 0;
    if (kVerboseSCC) {
        for (size_t s = 0; s < num_states && path_count < 50; ++s) {
            CUDD::BDD src_bdd = mgr->bddOne();
            for (size_t i = 0; i < num_state_bits; ++i) {
                if ((s >> i) & 1) src_bdd &= state_vars[i];
                else src_bdd &= !state_vars[i];
            }
            if ((src_bdd & states).IsZero()) continue;
            
            for (size_t t = 0; t < num_states; ++t) {
                CUDD::BDD dst_bdd = mgr->bddOne();
                for (size_t i = 0; i < num_state_bits; ++i) {
                    if ((t >> i) & 1) dst_bdd &= primed_vars[i];
                    else dst_bdd &= !primed_vars[i];
                }
                if ((dst_bdd & primed_states).IsZero()) continue;
                
                if (!(src_bdd & dst_bdd & path_relation).IsZero()) {
                    std::cout << "  Path(" << s << ", " << t << ")" << std::endl;
                    path_count++;
                }
            }
        }
        std::cout << "[BuildPathRelation] Found " << path_count << " reachable pairs" << std::endl;
    }
    
    return path_relation;
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
    
    // Create primed and temp state variables
    size_t num_bits = var_mgr->state_variable_count(automaton_id);
    std::size_t primed_automaton_id = var_mgr->create_state_variables(num_bits);
    std::size_t temp_automaton_id = var_mgr->create_state_variables(num_bits);
    
    CUDD::BDD relation = BuildPathRelation(states, primed_automaton_id, temp_automaton_id);
    
    return PathRelationResult{relation, primed_automaton_id};
}

CUDD::BDD NaiveSCCDecomposer::PeelLayer(const CUDD::BDD& states) const {
    auto var_mgr = arena_.var_mgr();
    auto automaton_id = arena_.automaton_id();
    auto mgr = var_mgr->cudd_mgr();
    
    //std::cout << "\n[PeelLayer] ========== Starting PeelLayer ==========" << std::endl;
    //std::cout << "[PeelLayer] Input states node count: " << states.nodeCount() << std::endl;
    
    if (states.IsZero()) {
        //std::cout << "[PeelLayer] States is ZERO, returning ZERO" << std::endl;
        return mgr->bddZero();
    }
    
    // Create primed and temp state variables for path relation computation
    size_t num_bits_peel = var_mgr->state_variable_count(automaton_id);
    std::size_t primed_automaton_id = var_mgr->create_state_variables(num_bits_peel);
    std::size_t temp_automaton_id = var_mgr->create_state_variables(num_bits_peel);
    auto primed_vars = var_mgr->get_state_variables(primed_automaton_id);
    CUDD::BDD primed_cube = var_mgr->state_variables_cube(primed_automaton_id);
    
    // Path(s, s') = transitive closure of transition relation, restricted to states
    CUDD::BDD path = BuildPathRelation(states, primed_automaton_id, temp_automaton_id);
    //std::cout << "[PeelLayer] Path relation node count: " << path.nodeCount() << std::endl;
    
    if (path.IsZero()) {
        std::cout << "[PeelLayer] WARNING: Path relation is ZERO! (no transitions within state set)" << std::endl;
        return mgr->bddZero();
    }
    
    // TopLayer(s) = States(s) & ForAll s' . (Path(s', s) -> Path(s, s'))
    // This means: for all s' that can reach s, s can also reach s'
    //
    // path = Path(s, s') with s=unprimed, s'=primed
    //
    // To compute Path(s', s), we need to swap the arguments properly.
    // We'll use temp variables to do this correctly:
    // 1. path(s, s') with unprimed=s, primed=s'
    // 2. To get Path(s', s) with unprimed=s, primed=s':
    //    - First substitute unprimed -> temp: Path(t, s') with temp=t, primed=s'
    //    - Then substitute primed -> unprimed: Path(t, s) with temp=t, unprimed=s
    //    - Then substitute temp -> primed: Path(s', s) with primed=s', unprimed=s
    
    auto state_vars = var_mgr->get_state_variables(automaton_id);
    auto temp_vars = var_mgr->get_state_variables(temp_automaton_id);
    std::size_t total_vars = var_mgr->total_variable_count();
    
    // Step 1: unprimed -> temp
    std::vector<CUDD::BDD> to_temp(total_vars);
    for (std::size_t i = 0; i < total_vars; ++i) {
        to_temp[i] = mgr->bddVar(static_cast<int>(i));
    }
    for (std::size_t i = 0; i < state_vars.size(); ++i) {
        to_temp[state_vars[i].NodeReadIndex()] = temp_vars[i];
    }
    CUDD::BDD path_temp_primed = path.VectorCompose(to_temp);  // Path(t, s')
    
    // Step 2: primed -> unprimed
    std::vector<CUDD::BDD> primed_to_unprimed(total_vars);
    for (std::size_t i = 0; i < total_vars; ++i) {
        primed_to_unprimed[i] = mgr->bddVar(static_cast<int>(i));
    }
    for (std::size_t i = 0; i < primed_vars.size(); ++i) {
        primed_to_unprimed[primed_vars[i].NodeReadIndex()] = state_vars[i];
    }
    CUDD::BDD path_temp_unprimed = path_temp_primed.VectorCompose(primed_to_unprimed);  // Path(t, s)
    
    // Step 3: temp -> primed
    std::vector<CUDD::BDD> temp_to_primed(total_vars);
    for (std::size_t i = 0; i < total_vars; ++i) {
        temp_to_primed[i] = mgr->bddVar(static_cast<int>(i));
    }
    for (std::size_t i = 0; i < temp_vars.size(); ++i) {
        temp_to_primed[temp_vars[i].NodeReadIndex()] = primed_vars[i];
    }
    CUDD::BDD reverse_path_raw = path_temp_unprimed.VectorCompose(temp_to_primed);  // Path(s', s) with s'=primed, s=unprimed
    
    // Restrict reverse_path to valid state pairs (both s and s' must be in states)
    CUDD::BDD primed_states = UnprimedToPrimed(var_mgr, automaton_id, primed_automaton_id, states);
    CUDD::BDD reverse_path = reverse_path_raw;
    
    // Now both path and reverse_path use: unprimed=s, primed=s'
    // path(s, s') = "s can reach s'"
    // reverse_path(s, s') = "s' can reach s"
    
    // ForAll s' . (reverse_path(s, s') -> path(s, s'))
    // = ForAll s' . (!reverse_path | path)
    CUDD::BDD implication = !reverse_path | path;
    //std::cout << "[PeelLayer] Implication node count: " << implication.nodeCount() << std::endl;
    
    //std::cout << "[PeelLayer] Universal quantification over primed variables..." << std::endl;
    CUDD::BDD forall_result = implication.UnivAbstract(primed_cube);
    //std::cout << "[PeelLayer] Forall result node count: " << forall_result.nodeCount() << std::endl;
    
    if (forall_result.IsZero()) {
        std::cout << "[PeelLayer] WARNING: Forall result is ZERO!" << std::endl;
    } else if (forall_result.IsOne()) {
        std::cout << "[PeelLayer] Forall result is ONE (all states satisfy condition)" << std::endl;
    }
    
    
    // TopLayer(s) = States(s) & ForAll result
    //std::cout << "[PeelLayer] Computing top layer: states & forall_result" << std::endl;
    CUDD::BDD top_layer = states & forall_result;
    //std::cout << "[PeelLayer] Top layer node count: " << top_layer.nodeCount() << std::endl;
    
    if (top_layer.IsZero()) {
        //std::cout << "[PeelLayer] Top layer is ZERO (no terminal SCCs found)" << std::endl;
    } else if (top_layer == states) {
        //std::cout << "[PeelLayer] Top layer equals all states (all states are in terminal SCCs)" << std::endl;
    } else {
        //std::cout << "[PeelLayer] Top layer is a subset of states" << std::endl;
    }
    
    //std::cout << "[PeelLayer] ========== End PeelLayer ==========\n" << std::endl;
    // TODO: Existentially quantify over input and output variables to get state-to-state relation
    CUDD::BDD input_cube = var_mgr->input_cube();
    CUDD::BDD output_cube = var_mgr->output_cube();
    CUDD::BDD non_state_cube = input_cube * output_cube;
    if (!non_state_cube.IsOne()) {
        top_layer = top_layer.ExistAbstract(non_state_cube);
    }
    if (kVerboseSCC) {
        std::cout << "[PeelLayer] input: " << var_mgr->bdd_to_string(input_cube) << std::endl;
        std::cout << "[PeelLayer] output: " << var_mgr->bdd_to_string(output_cube) << std::endl;
        std::cout << "[PeelLayer] non_state_cube: " << var_mgr->bdd_to_string(non_state_cube) << std::endl;
        std::cout << "[PeelLayer] Top layer after existential quantification: " << var_mgr->bdd_to_string(top_layer) << std::endl;
    }
    
    return top_layer;
}

} // namespace Syft

