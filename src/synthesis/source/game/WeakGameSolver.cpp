#include "game/WeakGameSolver.h"
#include <algorithm>
#include <iostream>
#include <set>
#include <chrono>

namespace Syft {

WeakGameSolver::WeakGameSolver(const SymbolicStateDfa& arena, const CUDD::BDD& accepting_states, bool debug)
    : arena_(arena)
    , var_mgr_(arena.var_mgr())
    , accepting_states_(accepting_states)
    , decomposer_(std::make_unique<NaiveSCCDecomposer>(arena))
    , debug_(debug) {
}

static constexpr bool kVerboseSolver = false;

void WeakGameSolver::PrintStateSet(const std::string& name, const CUDD::BDD& states) const {
    if (!(debug_ && kVerboseSolver)) return;
    
    auto mgr = var_mgr_->cudd_mgr();
    auto automaton_id = arena_.automaton_id();
    size_t num_state_bits = var_mgr_->state_variable_count(automaton_id);
    auto state_vars = var_mgr_->get_state_variables(automaton_id);
    size_t num_states = 1ULL << num_state_bits;
    
    std::vector<size_t> state_list;
    for (size_t s = 0; s < num_states; ++s) {
        CUDD::BDD state_bdd = mgr->bddOne();
        for (size_t i = 0; i < num_state_bits; ++i) {
            if ((s >> i) & 1) {
                state_bdd &= state_vars[i];
            } else {
                state_bdd &= !state_vars[i];
            }
        }
        if (!(state_bdd & states).IsZero()) {
            state_list.push_back(s);
        }
    }
    
    std::cout << "[DEBUG] " << name << " (" << state_list.size() << " states) = {";
    for (size_t i = 0; i < state_list.size(); ++i) {
        if (i > 0) std::cout << ", ";
        std::cout << state_list[i];
    }
    std::cout << "}" << std::endl;
}

void WeakGameSolver::Initialize() const {
    if (initialized_) {
        return;
    }
    
    auto automaton_id = arena_.automaton_id();
    size_t num_bits = var_mgr_->state_variable_count(automaton_id);
    primed_automaton_id_ = var_mgr_->create_state_variables(num_bits);
    
    initialized_ = true;
}

CUDD::BDD WeakGameSolver::CPreSystem(const CUDD::BDD& target, const CUDD::BDD& state_space) const {
    Initialize();
    
    auto automaton_id = arena_.automaton_id();
    auto mgr = var_mgr_->cudd_mgr();
    // Use vector-compose approach: T(s,i,o) := next_state(s,i,o) ∈ target
    // Then quantify: CPre_system(X) = state_space & ∀inputs. ∃outputs. T
    auto transition_func = arena_.transition_function();
    auto transition_compose_vector = var_mgr_->make_compose_vector(automaton_id, transition_func);

    // Ensure we only consider pure-state target
    CUDD::BDD W = target & state_space;

    // T(s,i,o) is true when the next state is in W
    CUDD::BDD T = W.VectorCompose(transition_compose_vector);

    CUDD::BDD exists_output = T.ExistAbstract(var_mgr_->output_cube());
    CUDD::BDD forall_input = exists_output.UnivAbstract(var_mgr_->input_cube());

    if (debug_ && kVerboseSolver) {
        std::cout << "[DEBUG CPreSystem] target count: " << target.CountMinterm(var_mgr_->state_variable_count(automaton_id)) << std::endl;
    }

    return state_space & forall_input;
}

CUDD::BDD WeakGameSolver::CPreEnvironment(const CUDD::BDD& target, const CUDD::BDD& state_space) const {
    Initialize();
    
    auto automaton_id = arena_.automaton_id();
    auto mgr = var_mgr_->cudd_mgr();
    // Use vector-compose approach: T(s,i,o) := next_state(s,i,o) ∈ target
    // Then quantify: CPre_env(X) = state_space & ∀outputs. ∃inputs. T
    auto transition_func = arena_.transition_function();
    auto transition_compose_vector = var_mgr_->make_compose_vector(automaton_id, transition_func);

    CUDD::BDD W = target & state_space;
    CUDD::BDD T = W.VectorCompose(transition_compose_vector);

    CUDD::BDD exists_input = T.ExistAbstract(var_mgr_->input_cube());
    CUDD::BDD forall_output = exists_input.UnivAbstract(var_mgr_->output_cube());

    return state_space & forall_output;
}

CUDD::BDD WeakGameSolver::SolveReachability(const CUDD::BDD& goal_states,
                                            const CUDD::BDD& state_space) const {
    // μX. (goal ∩ state_space) ∪ CPre_s(X)
    CUDD::BDD winning = state_space & goal_states;
    
    while (true) {
        CUDD::BDD new_winning = winning | (state_space & CPreSystem(winning, state_space));
        
        if (new_winning == winning) {
            return winning;
        }
        
        winning = new_winning;
    }
}

CUDD::BDD WeakGameSolver::SolveSafety(const CUDD::BDD& safe_states,
                                       const CUDD::BDD& state_space) const {
    // νX. (safe ∩ state_space) ∩ CPre_s(X)
    CUDD::BDD winning = state_space & safe_states;
    
    while (true) {
        CUDD::BDD new_winning = winning & CPreSystem(winning, state_space);
        
        if (new_winning == winning) {
            return winning;
        }
        
        winning = new_winning;
    }
}

void WeakGameSolver::DumpDFA() const {
    auto mgr = var_mgr_->cudd_mgr();
    auto automaton_id = arena_.automaton_id();
    size_t num_state_bits = var_mgr_->state_variable_count(automaton_id);
    auto state_vars = var_mgr_->get_state_variables(automaton_id);
    auto transition_func = arena_.transition_function();
    
    std::cout << "[WeakGameSolver] ===== DFA DUMP =====" << std::endl;
    std::cout << "[WeakGameSolver] State bits: " << num_state_bits << std::endl;
    std::cout << "[WeakGameSolver] Input vars: " << var_mgr_->input_variable_count() << std::endl;
    std::cout << "[WeakGameSolver] Output vars: " << var_mgr_->output_variable_count() << std::endl;
    
    // Dump initial state
    CUDD::BDD initial = arena_.initial_state_bdd();
    std::cout << "[WeakGameSolver] Initial state BDD node count: " << initial.nodeCount() << std::endl;
    
    // Enumerate all states and transitions
    size_t num_states = 1 << num_state_bits;
    std::cout << "[WeakGameSolver] Total possible states: " << num_states << std::endl;
    
    // Dump accepting states
    std::cout << "[WeakGameSolver] Accepting states: {";
    bool first = true;
    for (size_t s = 0; s < num_states && s < 32; ++s) {
        // Build state BDD
        CUDD::BDD state_bdd = mgr->bddOne();
        for (size_t i = 0; i < num_state_bits; ++i) {
            if ((s >> i) & 1) {
                state_bdd &= state_vars[i];
            } else {
                state_bdd &= !state_vars[i];
            }
        }
        if (!(state_bdd & accepting_states_).IsZero()) {
            if (!first) std::cout << ", ";
            std::cout << s;
            first = false;
        }
    }
    std::cout << "}" << std::endl;
    
    // Get input/output variable counts
    size_t num_inputs = var_mgr_->input_variable_count();
    size_t num_outputs = var_mgr_->output_variable_count();
    CUDD::BDD input_cube = var_mgr_->input_cube();
    CUDD::BDD output_cube = var_mgr_->output_cube();
    size_t num_io = num_inputs + num_outputs;
    
    std::cout << "[WeakGameSolver] Input variable count: " << num_inputs << std::endl;
    std::cout << "[WeakGameSolver] Output variable count: " << num_outputs << std::endl;
    
    // Dump transitions (limit to small automata)
    if (num_states <= 16) {
        std::cout << "[WeakGameSolver] Transitions (state -> possible next states):" << std::endl;
        CUDD::BDD io_cube = input_cube * output_cube;
        
        for (size_t s = 0; s < num_states; ++s) {
            // Build state BDD
            CUDD::BDD state_bdd = mgr->bddOne();
            for (size_t i = 0; i < num_state_bits; ++i) {
                if ((s >> i) & 1) {
                    state_bdd &= state_vars[i];
                } else {
                    state_bdd &= !state_vars[i];
                }
            }
            
            std::set<size_t> next_states;
            for (size_t ns = 0; ns < num_states; ++ns) {
                // Check if there's any I/O that leads from s to ns
                CUDD::BDD next_match = mgr->bddOne();
                for (size_t i = 0; i < num_state_bits; ++i) {
                    if ((ns >> i) & 1) {
                        next_match &= transition_func[i];
                    } else {
                        next_match &= !transition_func[i];
                    }
                }
                
                CUDD::BDD can_reach = (state_bdd & next_match).ExistAbstract(io_cube);
                if (!can_reach.IsZero()) {
                    next_states.insert(ns);
                }
            }
            
            std::cout << "  " << s << " -> {";
            first = true;
            for (size_t ns : next_states) {
                if (!first) std::cout << ", ";
                std::cout << ns;
                first = false;
            }
            std::cout << "}" << std::endl;
        }
    } else {
        std::cout << "[WeakGameSolver] (Automaton too large to dump all transitions)" << std::endl;
    }
    
    std::cout << "[WeakGameSolver] ===== END DFA DUMP =====" << std::endl;
    
    // Dump machine-readable format for Python reconstruction
    //DumpDFAForPython();
}

void WeakGameSolver::DumpDFAForPython() const {
    auto mgr = var_mgr_->cudd_mgr();
    auto automaton_id = arena_.automaton_id();
    size_t num_state_bits = var_mgr_->state_variable_count(automaton_id);
    auto state_vars = var_mgr_->get_state_variables(automaton_id);
    auto transition_func = arena_.transition_function();
    size_t num_inputs = var_mgr_->input_variable_count();
    size_t num_outputs = var_mgr_->output_variable_count();
    
    std::cout << "===PYDFA_BEGIN===" << std::endl;
    std::cout << "num_state_bits=" << num_state_bits << std::endl;
    std::cout << "num_inputs=" << num_inputs << std::endl;
    std::cout << "num_outputs=" << num_outputs << std::endl;
    
    // State variable indices
    std::cout << "state_var_indices=";
    for (size_t i = 0; i < state_vars.size(); ++i) {
        if (i > 0) std::cout << ",";
        std::cout << state_vars[i].NodeReadIndex();
    }
    std::cout << std::endl;
    
    // Input variable labels and indices
    auto input_labels = var_mgr_->input_variable_labels();
    std::cout << "input_labels=";
    for (size_t i = 0; i < input_labels.size(); ++i) {
        if (i > 0) std::cout << ",";
        std::cout << input_labels[i];
    }
    std::cout << std::endl;
    
    // Output variable labels and indices
    auto output_labels = var_mgr_->output_variable_labels();
    std::cout << "output_labels=";
    for (size_t i = 0; i < output_labels.size(); ++i) {
        if (i > 0) std::cout << ",";
        std::cout << output_labels[i];
    }
    std::cout << std::endl;
    
    // Dump each transition function BDD as minterms
    // Format: for each (state, input, output) -> next_state_bit value
    // We enumerate all combinations and output which ones give 1
    
    // Total variables: state bits + num_inputs + num_outputs
    size_t total_vars = num_state_bits + num_inputs + num_outputs;
    size_t num_assignments = 1ULL << total_vars;
    
    for (size_t bit = 0; bit < transition_func.size(); ++bit) {
        std::cout << "trans_func_" << bit << "=";
        
        bool first = true;
        for (size_t assign = 0; assign < num_assignments; ++assign) {
            // Extract state, input, output from assignment
            size_t state_val = assign & ((1ULL << num_state_bits) - 1);
            size_t input_val = (assign >> num_state_bits) & ((1ULL << num_inputs) - 1);
            size_t output_val = (assign >> (num_state_bits + num_inputs)) & ((1ULL << num_outputs) - 1);
            
            // Build the assignment BDD using actual variable BDDs
            CUDD::BDD assignment = mgr->bddOne();
            
            // State variables
            for (size_t i = 0; i < num_state_bits; ++i) {
                if ((state_val >> i) & 1) {
                    assignment &= state_vars[i];
                } else {
                    assignment &= !state_vars[i];
                }
            }
            
            // For input/output, we need to use the cube and restrict
            // Since we don't have direct access to individual I/O var BDDs,
            // we'll use a different approach: enumerate via cube minterms
            
            // Actually, let's just use variable indices 0..num_inputs-1 for inputs
            // and num_inputs..num_inputs+num_outputs-1 for outputs
            // This assumes standard VarMgr layout
            for (size_t i = 0; i < num_inputs; ++i) {
                CUDD::BDD var = mgr->bddVar(static_cast<int>(i));
                if ((input_val >> i) & 1) {
                    assignment &= var;
                } else {
                    assignment &= !var;
                }
            }
            
            for (size_t i = 0; i < num_outputs; ++i) {
                CUDD::BDD var = mgr->bddVar(static_cast<int>(num_inputs + i));
                if ((output_val >> i) & 1) {
                    assignment &= var;
                } else {
                    assignment &= !var;
                }
            }
            
            // Check if transition_func[bit] is true under this assignment
            if (!(transition_func[bit] & assignment).IsZero()) {
                if (!first) std::cout << ";";
                // Output as: state_bits,input_bits,output_bits
                std::cout << state_val << "," << input_val << "," << output_val;
                first = false;
            }
        }
        std::cout << std::endl;
    }
    
    // Dump accepting states BDD as minterms
    std::cout << "accepting_minterms=";
    size_t num_states = 1ULL << state_vars.size();
    bool first = true;
    for (size_t s = 0; s < num_states; ++s) {
        CUDD::BDD state_bdd = mgr->bddOne();
        for (size_t i = 0; i < state_vars.size(); ++i) {
            if ((s >> i) & 1) {
                state_bdd &= state_vars[i];
            } else {
                state_bdd &= !state_vars[i];
            }
        }
        if (!(state_bdd & accepting_states_).IsZero()) {
            if (!first) std::cout << ";";
            for (size_t i = 0; i < state_vars.size(); ++i) {
                std::cout << ((s >> i) & 1);
            }
            first = false;
        }
    }
    std::cout << std::endl;
    
    // Dump initial state
    std::cout << "initial_minterm=";
    CUDD::BDD initial_bdd = arena_.initial_state_bdd();
    for (size_t s = 0; s < num_states; ++s) {
        CUDD::BDD state_bdd = mgr->bddOne();
        for (size_t i = 0; i < state_vars.size(); ++i) {
            if ((s >> i) & 1) {
                state_bdd &= state_vars[i];
            } else {
                state_bdd &= !state_vars[i];
            }
        }
        if (!(state_bdd & initial_bdd).IsZero()) {
            for (size_t i = 0; i < state_vars.size(); ++i) {
                std::cout << ((s >> i) & 1);
            }
            break;
        }
    }
    std::cout << std::endl;
    
    std::cout << "===PYDFA_END===" << std::endl;
}

WeakGameResult WeakGameSolver::Solve() const {
    auto mgr = var_mgr_->cudd_mgr();
    auto automaton_id = arena_.automaton_id();
    
    if (debug_ && kVerboseSolver) {
        std::cout << "[WeakGameSolver] Starting Solve()" << std::endl;
    }
    
    // Dump DFA info
    //DumpDFA();
    
    if (debug_ && kVerboseSolver) {
        std::cout << "[WeakGameSolver] Accepting states count: " << accepting_states_.CountMinterm(var_mgr_->state_variable_count(automaton_id)) << std::endl;
    }
    
    // Compute reachable states from initial state
    std::cout << "[WeakGameSolver] Starting reachability computation..." << std::endl;
    auto reachability_start = std::chrono::steady_clock::now();
    
    CUDD::BDD initial_state = arena_.initial_state_bdd();
    CUDD::BDD io_cube = var_mgr_->input_cube() * var_mgr_->output_cube();
    
    // Create primed variables for reachability computation
    size_t num_bits = var_mgr_->state_variable_count(automaton_id);
    std::size_t reach_primed_id = var_mgr_->create_state_variables(num_bits);
    auto primed_vars = var_mgr_->get_state_variables(reach_primed_id);
    auto unprimed_vars = var_mgr_->get_state_variables(automaton_id);
    CUDD::BDD primed_cube = var_mgr_->state_variables_cube(reach_primed_id);
    
    // Build transition relation: T(s, x, y, s') = AND_i(s_i' <-> f_i(s, x, y))
    std::cout << "[WeakGameSolver] Building transition relation for number of bits: " << num_bits        << std::endl;
    auto trans_rel_start = std::chrono::steady_clock::now();
    auto transition_func = arena_.transition_function();
    CUDD::BDD trans_rel = mgr->bddOne();
    for (std::size_t i = 0; i < transition_func.size(); ++i) {
        trans_rel &= primed_vars[i].Xnor(transition_func[i]);
    }
    auto trans_rel_end = std::chrono::steady_clock::now();
    auto trans_rel_duration = std::chrono::duration_cast<std::chrono::milliseconds>(trans_rel_end - trans_rel_start);
    std::cout << "[WeakGameSolver] Transition relation built in " << trans_rel_duration.count() << " ms" << std::endl;
    
    // Compute reachable states via fixpoint: Reach = μX. initial ∪ Post(X)
    // Post(X) = ∃s,x,y. X(s) ∧ T(s,x,y,s') [then rename s' to s]
    std::cout << "[WeakGameSolver] Computing reachability closure (fixpoint)..." << std::endl;
    auto closure_start = std::chrono::steady_clock::now();
    
    CUDD::BDD reachable = initial_state;
    
    // Build substitution to rename primed back to unprimed
    std::size_t total_vars = var_mgr_->total_variable_count();
    std::vector<CUDD::BDD> primed_to_unprimed(total_vars);
    for (std::size_t i = 0; i < total_vars; ++i) {
        primed_to_unprimed[i] = mgr->bddVar(static_cast<int>(i));
    }
    for (std::size_t i = 0; i < primed_vars.size(); ++i) {
        primed_to_unprimed[primed_vars[i].NodeReadIndex()] = unprimed_vars[i];
    }
    
    CUDD::BDD state_cube = var_mgr_->state_variables_cube(automaton_id);
    
    int closure_iterations = 0;
    while (true) {
        closure_iterations++;
        // Post(reachable) = ∃s,x,y. reachable(s) ∧ T(s,x,y,s')
        CUDD::BDD post = (reachable & trans_rel).ExistAbstract(state_cube * io_cube);
        // Rename s' to s
        post = post.VectorCompose(primed_to_unprimed);
        
        CUDD::BDD new_reachable = reachable | post;
        if (new_reachable == reachable) {
            break;
        }
        reachable = new_reachable;
    }
    
    auto closure_end = std::chrono::steady_clock::now();
    auto closure_duration = std::chrono::duration_cast<std::chrono::milliseconds>(closure_end - closure_start);
    std::cout << "[WeakGameSolver] Reachability closure completed in " << closure_duration.count() << " ms (" << closure_iterations << " iterations)" << std::endl;
    
    auto reachability_end = std::chrono::steady_clock::now();
    auto reachability_duration = std::chrono::duration_cast<std::chrono::milliseconds>(reachability_end - reachability_start);
    std::cout << "[WeakGameSolver] Total reachability computation: " << reachability_duration.count() << " ms" << std::endl;
    
    //PrintStateSet("Reachable states from initial", reachable);
    
    // Compute all layers and layers_below (like reference implementation)
    std::cout << "[WeakGameSolver] Starting SCC decomposition..." << std::endl;
    auto scc_start = std::chrono::steady_clock::now();
    
    std::vector<CUDD::BDD> layers;
    std::vector<CUDD::BDD> layers_below;
    
    // Start with reachable states only
    CUDD::BDD remaining = reachable;
    
    int layer_idx = 0;
    while (!remaining.IsZero()) {
        layers_below.push_back(remaining);  // States at this layer and below
        
        CUDD::BDD layer = decomposer_->PeelLayer(remaining);
        
        if (layer.IsZero()) {
            if (debug_ && kVerboseSolver) {
                std::cout << "[WeakGameSolver] PeelLayer returned empty, breaking" << std::endl;
            }
            // States remaining here have no internal transitions - they only transition
            // to already-peeled states. They will be handled via reachability/safety
            // from the layers they can reach.
            if (!remaining.IsZero()) {
                CUDD::BDD io_cube = var_mgr_->input_cube() * var_mgr_->output_cube();
                CUDD::BDD remaining_states = remaining.ExistAbstract(io_cube);
                //PrintStateSet("Orphan states (not in any SCC, will be handled via reachability)", remaining_states);
            }
            layers_below.pop_back();
            break;
        }
        
        // Ensure layer only depends on state variables (existentially quantify out I/O)
        CUDD::BDD io_cube = var_mgr_->input_cube() * var_mgr_->output_cube();
        CUDD::BDD layer_states = layer.ExistAbstract(io_cube);
        
        double layer_count = layer_states.CountMinterm(var_mgr_->state_variable_count(automaton_id));
        if (debug_ && kVerboseSolver) {
            std::cout << "[WeakGameSolver] Peeled layer " << layer_idx << " with " << layer_count << " states" << std::endl;
        }
        //PrintStateSet("Peeled layer " + std::to_string(layer_idx), layer_states);
        
        layers.push_back(layer_states);
        remaining = (remaining & !layer_states);
        //PrintStateSet("remaining after layer " + std::to_string(layer_idx), remaining);
        layer_idx++;
    }
    
    auto scc_end = std::chrono::steady_clock::now();
    auto scc_duration = std::chrono::duration_cast<std::chrono::milliseconds>(scc_end - scc_start);
    std::cout << "[WeakGameSolver] SCC decomposition completed in " << scc_duration.count() << " ms (" << layers.size() << " layers)" << std::endl;
    
    if (debug_ && kVerboseSolver) {
        std::cout << "[WeakGameSolver] Total layers: " << layers.size() << std::endl;
    }
        
    // Reverse layers so we process from bottom SCCs (terminal) to top (source)
    std::reverse(layers.begin(), layers.end());
    std::reverse(layers_below.begin(), layers_below.end());
    if (debug_ && kVerboseSolver) {
        std::cout << "[WeakGameSolver] Reversed layers (now processing bottom-up)" << std::endl;
    }
    
    // Initialize good_states and bad_states (matching reference implementation)
    CUDD::BDD good_states = mgr->bddZero();
    CUDD::BDD bad_states = mgr->bddZero();
    
    for (size_t i = 0; i < layers.size(); ++i) {
        const CUDD::BDD& layer = layers[i];
        const CUDD::BDD& state_space = layers_below[i];

        double layer_count = layer.CountMinterm(var_mgr_->state_variable_count(automaton_id));
        double space_count = state_space.CountMinterm(var_mgr_->state_variable_count(automaton_id));
        if (debug_ && kVerboseSolver) {
            std::cout << "[WeakGameSolver] Processing layer " << i << " (" << layer_count << " states, state_space=" << space_count << ")" << std::endl;
        }
        //PrintStateSet("layer", layer);
        //PrintStateSet("state_space (layers_below)", state_space);
        //PrintStateSet("good_states (before)", good_states);
        //PrintStateSet("bad_states (before)", bad_states);

        // Per-layer fixed point: accepting uses safety, rejecting uses reachability,
        // both re-evaluated against freshly discovered bad states in this layer.
        std::cout << "[WeakGameSolver] Starting fixed-point iteration for layer " << i << "..." << std::endl;
        auto fixedpoint_start = std::chrono::steady_clock::now();
        
        CUDD::BDD layer_good = mgr->bddZero();
        CUDD::BDD layer_bad = mgr->bddZero();
        
        int fixedpoint_iterations = 0;

        while (true) {
            fixedpoint_iterations++;
            CUDD::BDD local_bad = bad_states | layer_bad;
            CUDD::BDD local_good = good_states | layer_good;

            CUDD::BDD avoid_bad_states = SolveSafety(!local_bad, state_space);
            CUDD::BDD accepting_winners = accepting_states_ & avoid_bad_states;

            CUDD::BDD reach_target = local_good | accepting_winners;
            CUDD::BDD reach_good_states = SolveReachability(reach_target, state_space & !local_bad);

            CUDD::BDD new_layer_good = layer & (((!accepting_states_) & reach_good_states) |
                                                (accepting_states_ & avoid_bad_states));
            CUDD::BDD new_layer_bad = layer & !new_layer_good;

            if (new_layer_good == layer_good && new_layer_bad == layer_bad) {
                layer_good = new_layer_good;
                layer_bad = new_layer_bad;
                break;
            }

            layer_good = new_layer_good;
            layer_bad = new_layer_bad;
        }
        
        auto fixedpoint_end = std::chrono::steady_clock::now();
        auto fixedpoint_duration = std::chrono::duration_cast<std::chrono::milliseconds>(fixedpoint_end - fixedpoint_start);
        std::cout << "[WeakGameSolver] Fixed-point iteration for layer " << i << " completed in " << fixedpoint_duration.count() << " ms (" << fixedpoint_iterations << " iterations)" << std::endl;

        //PrintStateSet("layer_good", layer_good);
        //PrintStateSet("layer_bad (before update)", layer_bad);

        good_states |= layer_good;
        bad_states |= layer_bad;

        double total_good = good_states.CountMinterm(var_mgr_->state_variable_count(automaton_id));
        double total_bad = bad_states.CountMinterm(var_mgr_->state_variable_count(automaton_id));
        if (debug_ && kVerboseSolver) {
            std::cout << "[WeakGameSolver]   After layer " << i << ": good_states=" << total_good << ", bad_states=" << total_bad << std::endl;
        }
        //PrintStateSet("good_states (after)", good_states);
        //PrintStateSet("bad_states (after)", bad_states);
    }
    
    double final_good = good_states.CountMinterm(var_mgr_->state_variable_count(automaton_id));
    if (debug_ && kVerboseSolver) {
        std::cout << "[WeakGameSolver] Final winning states: " << final_good << std::endl;
    }
    //PrintStateSet("Final good_states", good_states);
    
    // Check initial state
    CUDD::BDD initial = arena_.initial_state_bdd();
    bool initial_winning = !(initial & !good_states).IsZero() == false;  // initial ⊆ good_states
    if (debug_ && kVerboseSolver) {
        std::cout << "[WeakGameSolver] Initial state is " << (initial_winning ? "WINNING" : "LOSING") << std::endl;
    }
    
    return WeakGameResult{good_states, good_states};
}

bool WeakGameSolver::IsWinning() const {
    WeakGameResult result = Solve();
    CUDD::BDD initial = arena_.initial_state_bdd();
    return !(initial & !result.winning_states).IsZero() == false;
}

} // namespace Syft

