#include "game/SCCDecomposer.h"
#include "VarMgr.h"
#include <vector>
#include <algorithm>
#include <cassert>
#include <queue>
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

    // transition functions f_i and primed variables s_i'
    auto transition_func = arena_.transition_function();
    auto primed_vars = var_mgr->get_state_variables(primed_automaton_id);

    // IO cube we want to abstract during merges
    CUDD::BDD io_cube = var_mgr->input_cube() * var_mgr->output_cube();

    // fast-return for empty transition
    if (transition_func.empty()) {
        return mgr->bddOne();
    }

    const bool log_enabled = spdlog::default_logger()->should_log(spdlog::level::debug);

    // Build per-bit equivalences (s_i' <-> f_i)
    std::vector<CUDD::BDD> initial;
    initial.reserve(transition_func.size());
    for (std::size_t i = 0; i < transition_func.size(); ++i) {
        if (log_enabled) spdlog::debug("[NaiveSCCDecomposer] Building per-bit equivalence: {} / {} bits",
                                       (i + 1), transition_func.size());
        initial.emplace_back(primed_vars[i].Xnor(transition_func[i]));
        if (log_enabled) spdlog::debug("[NaiveSCCDecomposer]   BDD node count: {}", initial.back().nodeCount());
    }

    // If there's only one bit, just abstract IO from it and return
    if (initial.size() == 1) {
        CUDD::BDD single = initial.front().ExistAbstract(io_cube);
        if (log_enabled) spdlog::debug("[NaiveSCCDecomposer] Single-bit relation node count after abstract: {}",
                                       single.nodeCount());
        return single;
    }

    // Min-heap (smallest nodeCount first) -> Huffman-like merging
    struct Item { CUDD::BDD b; std::size_t sz; };
    auto cmp = [](Item const& a, Item const& b) { return a.sz > b.sz; };
    std::priority_queue<Item, std::vector<Item>, decltype(cmp)> pq(cmp);

    for (auto &bb : initial) {
        pq.push(Item{ std::move(bb), bb.nodeCount() });
    }
    initial.clear();

    // limit for AndAbstract: tune this to abort overly large intermediate ops.
    // UINT_MAX = no practical limit. Lower if you want fail-fast behavior.
    const unsigned and_abstract_limit = std::numeric_limits<unsigned>::max();

    std::size_t step = 0;
    while (pq.size() >= 2) {
        auto A = std::move(pq.top()); pq.pop();
        auto B = std::move(pq.top()); pq.pop();

        if (log_enabled) {
            spdlog::debug("[NaiveSCCDecomposer] Merging step {}: sizes {} + {}", step++, A.sz, B.sz);
        }

        // Log a and b
        if (log_enabled) {
            spdlog::debug("[NaiveSCCDecomposer]   => A node count: {}", A.b.nodeCount());
            spdlog::debug("[NaiveSCCDecomposer]   => B node count: {}", B.b.nodeCount());
        }
        // Use the provided member AndAbstract: (A.b & B.b) with IO variables abstracted in one go.
        // Signature: BDD::AndAbstract(const BDD& g, const BDD& cube, unsigned int limit) const
        // We call it on A.b: A.b.AndAbstract(B.b, io_cube, and_abstract_limit)
        CUDD::BDD merged = A.b.AndAbstract(B.b, io_cube, 0);

        if (log_enabled) {
            spdlog::debug("[NaiveSCCDecomposer]   => merged node count: {}", merged.nodeCount());
        }

        pq.push(Item{ std::move(merged), merged.nodeCount() });

        // Optional: if you see memory pressure, you can hint GC or reordering here (commented).
        // mgr->garbageCollect(); // uncomment if you have a wrapper exposing this and it helps
        // mgr->ReduceHeap(CUDD_REORDER_SIFT); // expensive: use sparingly
    }

    // The top now contains the fully-merged relation with IO already abstracted.
    CUDD::BDD trans_relation = std::move(pq.top().b);
    if (log_enabled) spdlog::debug("[NaiveSCCDecomposer] Built transition relation final node count: {}",
                                   trans_relation.nodeCount());

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
    std::cout << "[ComposeRelations] Composition node count: " << composition.nodeCount() << std::endl;
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
        
        // Safety check to prevent infinite loops
        if (iteration > 3000) {
            break;
        }
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
        std::cout << "[BuildPathRelation] Using cached path relation and restricting to current state set" << std::endl;
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
    
    //std::cout << "\n[PeelLayer] ========== Starting PeelLayer ==========" << std::endl;
    //std::cout << "[PeelLayer] Input states node count: " << states.nodeCount() << std::endl;
    
    if (states.IsZero()) {
        //std::cout << "[PeelLayer] States is ZERO, returning ZERO" << std::endl;
        return mgr->bddZero();
    }
    
    // Use cached path relation (initialize on demand) and get primed vars
    PathRelationResult path_res = BuildPathRelationWithPrimed(states);
    CUDD::BDD path = path_res.relation;
    std::size_t primed_automaton_id = path_res.primed_automaton_id;
    auto primed_vars = var_mgr->get_state_variables(primed_automaton_id);
    CUDD::BDD primed_cube = var_mgr->state_variables_cube(primed_automaton_id);
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
    auto temp_vars = var_mgr->get_state_variables(temp_automaton_id_);
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

