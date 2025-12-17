#include "catch2/catch_test_macros.hpp"
#include "catch2/generators/catch_generators_all.hpp"
#include "game/SCCDecomposer.h"
#include "automata/SymbolicStateDfa.h"
#include "automata/ExplicitStateDfaAdd.h"
#include "automata/ExplicitStateDfa.h"
#include "VarMgr.h"
#include <vector>
#include <set>
#include <sstream>
#include <memory>

extern "C" {
#include <mona/bdd.h>
#include <mona/dfa.h>
#include <mona/mem.h>
}

// Helper to create a state BDD from state number
CUDD::BDD state_to_bdd(int state, const std::vector<CUDD::BDD>& state_vars, 
                       std::shared_ptr<Syft::VarMgr> var_mgr, std::size_t automaton_id) {
    size_t num_bits = var_mgr->state_variable_count(automaton_id);
    std::vector<int> binary = Syft::SymbolicStateDfa::state_to_binary(state, num_bits);
    CUDD::BDD state_bdd = var_mgr->cudd_mgr()->bddOne();
    for (size_t bit = 0; bit < num_bits; ++bit) {
        if (binary[bit]) {
            state_bdd &= state_vars[bit];
        } else {
            state_bdd &= !state_vars[bit];
        }
    }
    return state_bdd;
}

TEST_CASE("Transition Relation Test", "[transition]")
{
    // Same graph as SCC Decomposition Test
    const int num_states = 10;
    const int num_vars = 1;
    
    std::vector<std::vector<int>> transitions = {
        {1},      // 0 -> 1
        {0, 2},   // 1 -> 0, 2
        {3, 6},   // 2 -> 3, 6
        {8, 4},   // 3 -> 8, 4
        {5},      // 4 -> 5
        {4},      // 5 -> 4
        {7},      // 6 -> 7
        {5, 6},   // 7 -> 5, 6
        {9},      // 8 -> 9
        {8}       // 9 -> 8
    };
    
    int indices[1] = {0};
    dfaSetup(num_states, num_vars, indices);
    
    std::string statuses_str;
    for (int state = 0; state < num_states; ++state) {
        statuses_str += "-";
        int num_trans = transitions[state].size();
        dfaAllocExceptions(num_trans);
        int trans_idx = 0;
        for (int target : transitions[state]) {
            std::string guard_str = (trans_idx % 2 == 0) ? "0" : "1";
            std::vector<char> guard(guard_str.begin(), guard_str.end());
            guard.push_back('\0');
            dfaStoreException(target, guard.data());
            trans_idx++;
        }
        int default_target = (num_trans > 0) ? transitions[state][0] : state;
        dfaStoreState(default_target);
    }
    
    std::vector<char> statuses(statuses_str.begin(), statuses_str.end());
    statuses.push_back('\0');
    DFA* mona_dfa = dfaBuild(statuses.data());
    
    std::vector<std::string> dfa_var_names = {"dummy"};
    Syft::ExplicitStateDfa explicit_dfa(mona_dfa, dfa_var_names);
    
    std::shared_ptr<Syft::VarMgr> var_mgr = std::make_shared<Syft::VarMgr>();
    var_mgr->create_named_variables(dfa_var_names);
    var_mgr->partition_variables(dfa_var_names, {});
    Syft::ExplicitStateDfaAdd explicit_dfa_add = Syft::ExplicitStateDfaAdd::from_dfa_mona(var_mgr, explicit_dfa);
    Syft::SymbolicStateDfa symbolic_dfa = Syft::SymbolicStateDfa::from_explicit(std::move(explicit_dfa_add));
    
    auto var_mgr_ptr = symbolic_dfa.var_mgr();
    auto automaton_id = symbolic_dfa.automaton_id();
    auto state_vars = var_mgr_ptr->get_state_variables(automaton_id);
    auto mgr = var_mgr_ptr->cudd_mgr();
    
    // Use the NaiveSCCDecomposer to build the transition relation
    Syft::NaiveSCCDecomposer decomposer(symbolic_dfa);
    auto result = decomposer.BuildTransitionRelationWithPrimed();
    
    CUDD::BDD trans_rel = result.relation;
    std::size_t primed_id = result.primed_automaton_id;
    auto primed_vars = var_mgr_ptr->get_state_variables(primed_id);
    
    // Verify each expected transition exists
    for (int from = 0; from < num_states; ++from) {
        CUDD::BDD from_bdd = state_to_bdd(from, state_vars, var_mgr_ptr, automaton_id);
        
        for (int to = 0; to < num_states; ++to) {
            CUDD::BDD to_bdd = state_to_bdd(to, primed_vars, var_mgr_ptr, primed_id);
            
            CUDD::BDD edge = trans_rel & from_bdd & to_bdd;
            bool edge_exists = !edge.IsZero();
            
            // Check if this transition should exist
            bool should_exist = false;
            for (int target : transitions[from]) {
                if (target == to) {
                    should_exist = true;
                    break;
                }
            }
            
            INFO("Transition " << from << " -> " << to << ": exists=" << edge_exists << ", expected=" << should_exist);
            REQUIRE(edge_exists == should_exist);
        }
    }
}

TEST_CASE("Path Relation Test", "[path]")
{
    // Same graph as Transition Relation Test
    const int num_states = 10;
    const int num_vars = 1;
    
    std::vector<std::vector<int>> transitions = {
        {1},      // 0 -> 1
        {0, 2},   // 1 -> 0, 2
        {3, 6},   // 2 -> 3, 6
        {8, 4},   // 3 -> 8, 4
        {5},      // 4 -> 5
        {4},      // 5 -> 4
        {7},      // 6 -> 7
        {5, 6},   // 7 -> 5, 6
        {9},      // 8 -> 9
        {8}       // 9 -> 8
    };
    
    int indices[1] = {0};
    dfaSetup(num_states, num_vars, indices);
    
    std::string statuses_str;
    for (int state = 0; state < num_states; ++state) {
        statuses_str += "-";
        int num_trans = transitions[state].size();
        dfaAllocExceptions(num_trans);
        int trans_idx = 0;
        for (int target : transitions[state]) {
            std::string guard_str = (trans_idx % 2 == 0) ? "0" : "1";
            std::vector<char> guard(guard_str.begin(), guard_str.end());
            guard.push_back('\0');
            dfaStoreException(target, guard.data());
            trans_idx++;
        }
        int default_target = (num_trans > 0) ? transitions[state][0] : state;
        dfaStoreState(default_target);
    }
    
    std::vector<char> statuses(statuses_str.begin(), statuses_str.end());
    statuses.push_back('\0');
    DFA* mona_dfa = dfaBuild(statuses.data());
    
    std::vector<std::string> dfa_var_names = {"dummy"};
    Syft::ExplicitStateDfa explicit_dfa(mona_dfa, dfa_var_names);
    
    std::shared_ptr<Syft::VarMgr> var_mgr = std::make_shared<Syft::VarMgr>();
    var_mgr->create_named_variables(dfa_var_names);
    var_mgr->partition_variables(dfa_var_names, {});
    Syft::ExplicitStateDfaAdd explicit_dfa_add = Syft::ExplicitStateDfaAdd::from_dfa_mona(var_mgr, explicit_dfa);
    Syft::SymbolicStateDfa symbolic_dfa = Syft::SymbolicStateDfa::from_explicit(std::move(explicit_dfa_add));
    
    auto var_mgr_ptr = symbolic_dfa.var_mgr();
    auto automaton_id = symbolic_dfa.automaton_id();
    auto state_vars = var_mgr_ptr->get_state_variables(automaton_id);
    auto mgr = var_mgr_ptr->cudd_mgr();
    
    // Build all states BDD
    size_t num_bits = var_mgr_ptr->state_variable_count(automaton_id);
    CUDD::BDD all_states = mgr->bddZero();
    for (int state = 0; state < num_states; ++state) {
        all_states |= state_to_bdd(state, state_vars, var_mgr_ptr, automaton_id);
    }
    
    // Compute expected reachability (transitive closure of transitions)
    // Note: a state can reach itself only if there's an actual cycle back to it
    std::vector<std::set<int>> reachable(num_states);
    for (int i = 0; i < num_states; ++i) {
        // BFS from state i - don't include i initially, only if we find a path back
        std::vector<bool> visited(num_states, false);
        std::vector<int> queue;
        // Start with successors of i
        for (int next : transitions[i]) {
            if (!visited[next]) {
                visited[next] = true;
                queue.push_back(next);
                reachable[i].insert(next);
            }
        }
        while (!queue.empty()) {
            int curr = queue.back();
            queue.pop_back();
            for (int next : transitions[curr]) {
                if (!visited[next]) {
                    visited[next] = true;
                    queue.push_back(next);
                    reachable[i].insert(next);
                } else if (next == i) {
                    // Found a cycle back to i
                    reachable[i].insert(i);
                }
            }
        }
    }
    
    // Use the NaiveSCCDecomposer to build the path relation
    Syft::NaiveSCCDecomposer decomposer(symbolic_dfa);
    auto result = decomposer.BuildPathRelationWithPrimed(all_states);
    
    CUDD::BDD path_rel = result.relation;
    std::size_t primed_id = result.primed_automaton_id;
    auto primed_vars = var_mgr_ptr->get_state_variables(primed_id);
    
    // Verify reachability
    for (int from = 0; from < num_states; ++from) {
        CUDD::BDD from_bdd = state_to_bdd(from, state_vars, var_mgr_ptr, automaton_id);
        
        for (int to = 0; to < num_states; ++to) {
            CUDD::BDD to_bdd = state_to_bdd(to, primed_vars, var_mgr_ptr, primed_id);
            
            CUDD::BDD edge = path_rel & from_bdd & to_bdd;
            bool reachable_in_bdd = !edge.IsZero();
            bool should_be_reachable = reachable[from].count(to) > 0;
            
            INFO("Reachability " << from << " -> " << to << ": in_bdd=" << reachable_in_bdd << ", expected=" << should_be_reachable);
            REQUIRE(reachable_in_bdd == should_be_reachable);
        }
    }
}

TEST_CASE("SCC Decomposition Test", "[scc]")
{
    // Graph structure:
    // 1 -> 2, 2 -> 1, 2 -> 3, 3 -> 4, 4 -> 9, 9 -> 10, 10 -> 9, 4 -> 5, 5 <-> 6, 8 -> 6, 7 -> 8, 8 -> 7, 3 -> 7
    // States: 1-10 (we'll use 0-9 internally)
    // Expected SCCs: {0,1}, {4,5}, {6,7}, {8,9}, and singletons {2}, {3}
    INFO("Starting SCC Decomposition Test");

    const int num_states = 10;
    
    // We need at least one variable for the alphabet (even if it's a dummy)
    // For a pure graph with no alphabet, we'll use one variable that's always true
    const int num_vars = 1;
    
    // Define transitions: from_state -> {to_states}
    std::vector<std::vector<int>> transitions = {
        {1},      // 0 -> 1
        {0, 2},   // 1 -> 0, 2
        {3, 6},   // 2 -> 3, 6
        {8, 4},   // 3 -> 8, 4
        {5},      // 4 -> 5
        {6},      // 5 -> 6
        {7},      // 6 -> 7
        {5, 6},   // 7 -> 5, 6
        {9},      // 8 -> 9
        {8}       // 9 -> 8
    };
    
    // Setup DFA structure
    int indices[1] = {0};
    dfaSetup(num_states, num_vars, indices);
    
    // Build status string (+ for final, - for non-final)
    // We'll make all states non-final for this test
    std::string statuses_str;
    
    // For each state, store transitions and build status string
    for (int state = 0; state < num_states; ++state) {
        statuses_str += "-"; // Non-final state
        
        // Allocate space for transitions
        int num_trans = transitions[state].size();
        dfaAllocExceptions(num_trans);
        
        // For each transition, create a unique guard
        // MONA DFAs are deterministic, so each transition needs a different guard
        // For one variable, we can use "0" and "1" to distinguish transitions
        // Guard format: string of bits where '0'/'1' means variable is false/true
        int trans_idx = 0;
        for (int target : transitions[state]) {
            // Use "0" for first transition, "1" for second, etc.
            // Since we only have one variable, we alternate between "0" and "1"
            // For states with only one transition, we use "0" (the other input goes to default)
            std::string guard_str = (trans_idx % 2 == 0) ? "0" : "1";
            // Need non-const char* for MONA API
            std::vector<char> guard(guard_str.begin(), guard_str.end());
            guard.push_back('\0');
            
            dfaStoreException(target, guard.data());
            trans_idx++;
        }
        
        // Set default state: for states with one transition, default goes to the same target
        // For states with multiple transitions, default goes to the first target
        // This ensures all transitions are reachable
        int default_target = (num_trans > 0) ? transitions[state][0] : state;
        dfaStoreState(default_target);
    }
    // Build the DFA - need non-const char* for MONA API
    std::vector<char> statuses(statuses_str.begin(), statuses_str.end());
    statuses.push_back('\0');
    DFA* mona_dfa = dfaBuild(statuses.data());
    
    // Create ExplicitStateDfa
    std::vector<std::string> dfa_var_names = {"dummy"};
    Syft::ExplicitStateDfa explicit_dfa(mona_dfa, dfa_var_names);
    
    // Convert to ExplicitStateDfaAdd
    std::shared_ptr<Syft::VarMgr> var_mgr = std::make_shared<Syft::VarMgr>();
    var_mgr->create_named_variables(dfa_var_names);
    var_mgr->partition_variables(dfa_var_names, {});  // "dummy" is an input variable
    Syft::ExplicitStateDfaAdd explicit_dfa_add = Syft::ExplicitStateDfaAdd::from_dfa_mona(var_mgr, explicit_dfa);
    
    // Convert to SymbolicStateDfa
    Syft::SymbolicStateDfa symbolic_dfa = Syft::SymbolicStateDfa::from_explicit(std::move(explicit_dfa_add));
    
    // Test both implementations - use a parameter to choose
    // For now, test both and verify they produce the same results
    bool use_naive = GENERATE(true, false);
    INFO("Using " << (use_naive ? "Naive" : "Chain") << " SCC Decomposer");
    
    // Create SCC decomposer based on choice
    std::unique_ptr<Syft::SCCDecomposer> decomposer;
    if (use_naive) {
        decomposer = std::make_unique<Syft::NaiveSCCDecomposer>(symbolic_dfa);
    } else {
        decomposer = std::make_unique<Syft::ChainSCCDecomposer>(symbolic_dfa);
    }
    
    // Get all states as a BDD
    auto var_mgr_ptr = symbolic_dfa.var_mgr();
    auto automaton_id = symbolic_dfa.automaton_id();
    auto state_vars = var_mgr_ptr->get_state_variables(automaton_id);
    auto mgr = var_mgr_ptr->cudd_mgr();
    INFO("Set up completed");

    // Get the number of bits used for state encoding
    size_t num_bits = var_mgr_ptr->state_variable_count(automaton_id);
    
    CUDD::BDD all_states = mgr->bddZero();
    
    // Create BDD for all states (0-9)
    for (int state = 0; state < num_states; ++state) {
        std::vector<int> binary = Syft::SymbolicStateDfa::state_to_binary(state, num_bits);
        CUDD::BDD state_bdd = mgr->bddOne();
        for (size_t bit = 0; bit < num_bits; ++bit) {
            if (binary[bit]) {
                state_bdd *= state_vars[bit];
            } else {
                state_bdd *= !state_vars[bit];
            }
        }
        all_states |= state_bdd;
    }
    
    // Test SCC decomposition - peel layers
    CUDD::BDD remaining = all_states;
    std::vector<std::set<int>> found_layers;
    
    while (!((all_states & remaining).IsZero())) {
        CUDD::BDD layer = decomposer->PeelLayer(remaining);
        
        if (layer.IsZero()) {
            break;
        }
        
        // Extract states from the layer
        std::set<int> layer_states;
        for (int state = 0; state < num_states; ++state) {
            std::vector<int> binary = Syft::SymbolicStateDfa::state_to_binary(state, num_bits);
            CUDD::BDD state_bdd = mgr->bddOne();
            for (size_t bit = 0; bit < num_bits; ++bit) {
                if (binary[bit]) {
                    state_bdd *= state_vars[bit];
                } else {
                    state_bdd *= !state_vars[bit];
                }
            }
            
            if (!(layer * state_bdd).IsZero()) {
                layer_states.insert(state);
            }
        }
        
        if (!layer_states.empty()) {
            found_layers.push_back(layer_states);
        }
        
        remaining = remaining & !layer;
    }
    
    // Verify expected layer order:
    // L1: {8,9}, {4,5} (terminal SCCs - no outgoing edges to other SCCs)
    // L2: {3}, {6,7}
    // L3: {2}
    // L4: {0,1}
    
    INFO("Found " << found_layers.size() << " SCC layers");
    //REQUIRE(found_layers.size() >= 4);
    
    // L1: Should contain only {8,9} and {4,5}
    std::set<int> layer1_expected = {4, 5, 8, 9};
    REQUIRE(found_layers[0] == layer1_expected);
    INFO("Layer 1 verified: contains only states {4,5,8,9}");
    
    // Note: Layer 1 might contain {8,9} and {4,5} as separate SCCs in the same layer BDD
    // We verify they are both present by checking the union equals the expected set
    
    // L2: Should contain only {3} and {6,7}
    std::set<int> layer2_expected = {3, 6, 7};
    REQUIRE(found_layers[1] == layer2_expected);
    INFO("Layer 2 verified: contains only states {3,6,7}");
    
    // L3: Should contain only {2}
    std::set<int> layer3_expected = {2};
    REQUIRE(found_layers[2] == layer3_expected);
    INFO("Layer 3 verified: contains state {2}");
    
    // L4: Should contain only {0,1}
    std::set<int> layer4_expected = {0, 1};
    REQUIRE(found_layers[3] == layer4_expected);
    INFO("Layer 4 verified: contains states {0,1}");
    
    INFO("SCC Decomposition Test completed - all layers verified in correct order");
}
