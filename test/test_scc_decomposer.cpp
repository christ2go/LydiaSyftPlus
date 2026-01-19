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
#include <functional>
#include <random>

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

// Standard test graph transitions
const std::vector<std::vector<int>>& get_test_transitions() {
    static const std::vector<std::vector<int>> transitions = {
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
    return transitions;
}

// Helper to create the standard test DFA
Syft::SymbolicStateDfa create_test_dfa() {
    const auto& transitions = get_test_transitions();
    const int num_states = transitions.size();
    const int num_vars = 1;
    
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
    
    return Syft::SymbolicStateDfa::from_explicit(std::move(explicit_dfa_add));
}

TEST_CASE("Transition Relation Test", "[transition]")
{
    const auto& transitions = get_test_transitions();
    const int num_states = transitions.size();
    
    Syft::SymbolicStateDfa symbolic_dfa = create_test_dfa();
    
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

TEST_CASE("Transition Relation Random Graph Test", "[transitionrand]")
{
    const int num_states = 200;
    const int max_transitions = 6;
    const int num_vars = 3;  // 3 variables encode up to 8 guarded transitions

    std::mt19937 gen(12345);
    std::uniform_int_distribution<int> edge_count_dist(1, max_transitions);
    std::uniform_int_distribution<int> target_dist(0, num_states - 1);

    std::vector<std::vector<int>> transitions(num_states);
    for (int state = 0; state < num_states; ++state) {
        int num_trans = edge_count_dist(gen);
        std::set<int> targets;
        while (static_cast<int>(targets.size()) < num_trans) {
            targets.insert(target_dist(gen));
        }
        transitions[state].assign(targets.begin(), targets.end());
    }

    int indices[3] = {0, 1, 2};
    dfaSetup(num_states, num_vars, indices);

    std::string statuses_str(num_states, '-');
    for (int state = 0; state < num_states; ++state) {
        int num_trans = transitions[state].size();
        dfaAllocExceptions(num_trans);
        int trans_idx = 0;
        for (int target : transitions[state]) {
            std::string guard_str;
            guard_str += ((trans_idx & 4) ? '1' : '0');
            guard_str += ((trans_idx & 2) ? '1' : '0');
            guard_str += ((trans_idx & 1) ? '1' : '0');
            std::vector<char> guard(guard_str.begin(), guard_str.end());
            guard.push_back('\0');
            dfaStoreException(target, guard.data());
            trans_idx++;
        }
        int default_target = transitions[state].empty() ? state : transitions[state][0];
        dfaStoreState(default_target);
    }

    std::vector<char> statuses(statuses_str.begin(), statuses_str.end());
    statuses.push_back('\0');
    DFA* mona_dfa = dfaBuild(statuses.data());

    std::vector<std::string> dfa_var_names = {"v0", "v1", "v2"};
    Syft::ExplicitStateDfa explicit_dfa(mona_dfa, dfa_var_names);

    std::shared_ptr<Syft::VarMgr> var_mgr = std::make_shared<Syft::VarMgr>();
    var_mgr->create_named_variables(dfa_var_names);
    var_mgr->partition_variables(dfa_var_names, {});
    Syft::ExplicitStateDfaAdd explicit_dfa_add = Syft::ExplicitStateDfaAdd::from_dfa_mona(var_mgr, explicit_dfa);
    Syft::SymbolicStateDfa symbolic_dfa = Syft::SymbolicStateDfa::from_explicit(std::move(explicit_dfa_add));

    auto var_mgr_ptr = symbolic_dfa.var_mgr();
    auto automaton_id = symbolic_dfa.automaton_id();
    auto state_vars = var_mgr_ptr->get_state_variables(automaton_id);

    Syft::NaiveSCCDecomposer decomposer(symbolic_dfa);
    auto result = decomposer.BuildTransitionRelationWithPrimed();

    CUDD::BDD trans_rel = result.relation;
    std::size_t primed_id = result.primed_automaton_id;
    auto primed_vars = var_mgr_ptr->get_state_variables(primed_id);

    for (int from = 0; from < num_states; ++from) {
        CUDD::BDD from_bdd = state_to_bdd(from, state_vars, var_mgr_ptr, automaton_id);

        for (int to = 0; to < num_states; ++to) {
            CUDD::BDD to_bdd = state_to_bdd(to, primed_vars, var_mgr_ptr, primed_id);

            CUDD::BDD edge = trans_rel & from_bdd & to_bdd;
            bool edge_exists = !edge.IsZero();

            bool should_exist = false;
            for (int target : transitions[from]) {
                if (target == to) {
                    should_exist = true;
                    break;
                }
            }

            INFO("Transition " << from << " -> " << to << ": exists=" << edge_exists
                 << ", expected=" << should_exist);
            REQUIRE(edge_exists == should_exist);
        }
    }
}

TEST_CASE("Path Relation Test", "[path]")
{
    const auto& transitions = get_test_transitions();
    const int num_states = transitions.size();
    
    Syft::SymbolicStateDfa symbolic_dfa = create_test_dfa();
    
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

TEST_CASE("Restricted Path Relation Test", "[path_restricted]")
{
    const auto& transitions = get_test_transitions();
    const int num_states = transitions.size();
    
    Syft::SymbolicStateDfa symbolic_dfa = create_test_dfa();
    
    auto var_mgr_ptr = symbolic_dfa.var_mgr();
    auto automaton_id = symbolic_dfa.automaton_id();
    auto state_vars = var_mgr_ptr->get_state_variables(automaton_id);
    auto mgr = var_mgr_ptr->cudd_mgr();
    
    size_t num_bits = var_mgr_ptr->state_variable_count(automaton_id);
    
    // Build restricted states BDD: only {4,5,6,7,8,9}
    std::set<int> restricted_states_set = {4, 5, 6, 7, 8, 9};
    CUDD::BDD restricted_states = mgr->bddZero();
    for (int state : restricted_states_set) {
        restricted_states |= state_to_bdd(state, state_vars, var_mgr_ptr, automaton_id);
    }
    
    // Compute expected reachability within restricted states only
    // A path exists from i to j only if all intermediate states are also in restricted_states_set
    std::vector<std::set<int>> reachable(num_states);
    for (int i : restricted_states_set) {
        std::vector<bool> visited(num_states, false);
        std::vector<int> queue;
        // Start with successors of i that are in restricted set
        for (int next : transitions[i]) {
            if (restricted_states_set.count(next) && !visited[next]) {
                visited[next] = true;
                queue.push_back(next);
                reachable[i].insert(next);
            }
        }
        while (!queue.empty()) {
            int curr = queue.back();
            queue.pop_back();
            for (int next : transitions[curr]) {
                if (restricted_states_set.count(next) && !visited[next]) {
                    visited[next] = true;
                    queue.push_back(next);
                    reachable[i].insert(next);
                } else if (next == i && restricted_states_set.count(next)) {
                    reachable[i].insert(i);
                }
            }
        }
    }
    
    // Use the NaiveSCCDecomposer to build the restricted path relation
    Syft::NaiveSCCDecomposer decomposer(symbolic_dfa);
    auto result = decomposer.BuildPathRelationWithPrimed(restricted_states);
    
    CUDD::BDD path_rel = result.relation;
    std::size_t primed_id = result.primed_automaton_id;
    auto primed_vars = var_mgr_ptr->get_state_variables(primed_id);
    
    // Verify reachability within restricted states
    for (int from : restricted_states_set) {
        CUDD::BDD from_bdd = state_to_bdd(from, state_vars, var_mgr_ptr, automaton_id);
        
        for (int to : restricted_states_set) {
            CUDD::BDD to_bdd = state_to_bdd(to, primed_vars, var_mgr_ptr, primed_id);
            
            CUDD::BDD edge = path_rel & from_bdd & to_bdd;
            bool reachable_in_bdd = !edge.IsZero();
            bool should_be_reachable = reachable[from].count(to) > 0;
            
            INFO("Restricted reachability " << from << " -> " << to << ": in_bdd=" << reachable_in_bdd << ", expected=" << should_be_reachable);
            REQUIRE(reachable_in_bdd == should_be_reachable);
        }
    }
    
    // Also verify that states outside restricted set are not in the relation
    for (int from = 0; from < 4; ++from) {
        CUDD::BDD from_bdd = state_to_bdd(from, state_vars, var_mgr_ptr, automaton_id);
        for (int to = 0; to < num_states; ++to) {
            CUDD::BDD to_bdd = state_to_bdd(to, primed_vars, var_mgr_ptr, primed_id);
            CUDD::BDD edge = path_rel & from_bdd & to_bdd;
            INFO("State " << from << " should not be in restricted relation");
            REQUIRE(edge.IsZero());
        }
    }
}

TEST_CASE("SCC Decomposition Test", "[scc]")
{
    INFO("Starting SCC Decomposition Test");

    const auto& transitions = get_test_transitions();
    const int num_states = transitions.size();
    
    Syft::SymbolicStateDfa symbolic_dfa = create_test_dfa();
    
    // Use Naive SCC Decomposer
    INFO("Using Naive SCC Decomposer");
    std::unique_ptr<Syft::SCCDecomposer> decomposer = std::make_unique<Syft::NaiveSCCDecomposer>(symbolic_dfa);
    
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
            std::cout << "Found layer " << found_layers.size() << ": { ";
            for (int s : layer_states) std::cout << s << " ";
            std::cout << "}" << std::endl;
            found_layers.push_back(layer_states);
        }
        
        remaining = remaining & !layer;
    }
    
    // Compute expected SCCs using Tarjan's algorithm
    std::vector<int> scc_id(num_states, -1);
    std::vector<int> low(num_states, -1), disc(num_states, -1);
    std::vector<bool> on_stack(num_states, false);
    std::vector<int> stack;
    std::vector<std::set<int>> sccs;
    int timer = 0;
    
    std::function<void(int)> tarjan = [&](int u) {
        low[u] = disc[u] = timer++;
        stack.push_back(u);
        on_stack[u] = true;
        
        for (int v : transitions[u]) {
            if (disc[v] == -1) {
                // Unvisited
                tarjan(v);
                low[u] = std::min(low[u], low[v]);
            } else if (on_stack[v]) {
                // Back edge
                low[u] = std::min(low[u], disc[v]);
            }
        }
        
        if (low[u] == disc[u]) {
            std::set<int> scc;
            while (true) {
                int v = stack.back();
                stack.pop_back();
                on_stack[v] = false;
                scc_id[v] = sccs.size();
                scc.insert(v);
                if (v == u) break;
            }
            sccs.push_back(scc);
        }
    };
    
    // Run Tarjan on all nodes
    for (int i = 0; i < num_states; ++i) {
        if (disc[i] == -1) {
            tarjan(i);
        }
    }
    
    // Print SCCs
    std::cout << "Found " << sccs.size() << " SCCs:" << std::endl;
    for (size_t i = 0; i < sccs.size(); ++i) {
        std::cout << "SCC " << i << ": { ";
        for (int s : sccs[i]) std::cout << s << " ";
        std::cout << "}" << std::endl;
    }
    
    // Compute SCC graph edges
    std::set<std::pair<int,int>> scc_edges;
    for (int u = 0; u < num_states; ++u) {
        for (int v : transitions[u]) {
            if (scc_id[u] != scc_id[v]) {
                scc_edges.insert({scc_id[u], scc_id[v]});
            }
        }
    }
    
    // Compute expected layers by peeling source SCCs (indegree 0)
    std::vector<std::set<int>> expected_layers;
    std::set<int> remaining_sccs;
    for (size_t i = 0; i < sccs.size(); ++i) {
        remaining_sccs.insert(i);
    }
    
    while (!remaining_sccs.empty()) {
        // Find source SCCs (no incoming edges from remaining SCCs)
        std::set<int> source_sccs;
        for (int scc : remaining_sccs) {
            bool is_source = true;
            for (auto& [from, to] : scc_edges) {
                if (to == scc && remaining_sccs.count(from) && from != scc) {
                    is_source = false;
                    break;
                }
            }
            if (is_source) {
                source_sccs.insert(scc);
            }
        }
        
        // Collect states in source SCCs
        std::set<int> layer_states;
        for (int scc : source_sccs) {
            for (int state : sccs[scc]) {
                layer_states.insert(state);
            }
            remaining_sccs.erase(scc);
        }
        
        if (!layer_states.empty()) {
            expected_layers.push_back(layer_states);
        }
    }
    // No need to reverse - we're already peeling from sources
    
    // Print expected layers
    std::cout << "Expected " << expected_layers.size() << " layers:" << std::endl;
    for (size_t i = 0; i < expected_layers.size(); ++i) {
        std::cout << "Expected layer " << i << ": { ";
        for (int s : expected_layers[i]) std::cout << s << " ";
        std::cout << "}" << std::endl;
    }
    
    INFO("Found " << found_layers.size() << " SCC layers, expected " << expected_layers.size());
    REQUIRE(found_layers.size() == expected_layers.size());
    
    for (size_t i = 0; i < expected_layers.size(); ++i) {
        std::ostringstream found_ss, expected_ss;
        found_ss << "{ ";
        for (int s : found_layers[i]) found_ss << s << " ";
        found_ss << "}";
        expected_ss << "{ ";
        for (int s : expected_layers[i]) expected_ss << s << " ";
        expected_ss << "}";
        INFO("Layer " << i << ": found " << found_ss.str() << ", expected " << expected_ss.str());
        REQUIRE(found_layers[i] == expected_layers[i]);
    }
    
    INFO("SCC Decomposition Test completed - all layers verified");
}

TEST_CASE("SCC Decomposition Random Graph Test", "[scc_random]")
{
    INFO("Starting SCC Decomposition Random Graph Test");

    const int num_states = 200;
    const int max_transitions = 6;  // at most 6 transitions per state
    const int num_vars = 3;  // 3 variables to represent up to 8 transitions
    
    // Generate random transitions
    std::vector<std::vector<int>> transitions(num_states);
    for (int state = 0; state < num_states; ++state) {
        int num_trans = 2 + (std::rand() % max_transitions);  // 1 to max_transitions
        std::set<int> targets;
        while ((int)targets.size() < num_trans) {
            targets.insert(std::rand() % num_states);
        }
        for (int t : targets) {
            transitions[state].push_back(t);
        }
    }
    
    // Setup DFA structure
    int indices[3] = {0, 1, 2};
    dfaSetup(num_states, num_vars, indices);
    
    std::string statuses_str;
    for (int state = 0; state < num_states; ++state) {
        statuses_str += "-";
        int num_trans = transitions[state].size();
        dfaAllocExceptions(num_trans);
        int trans_idx = 0;
        for (int target : transitions[state]) {
            // Encode trans_idx as 3-bit binary string
            std::string guard_str;
            guard_str += ((trans_idx & 4) ? '1' : '0');
            guard_str += ((trans_idx & 2) ? '1' : '0');
            guard_str += ((trans_idx & 1) ? '1' : '0');
            std::vector<char> guard(guard_str.begin(), guard_str.end());
            guard.push_back('\0');
            dfaStoreException(target, guard.data());
            trans_idx++;
        }
        int default_target = transitions[state][0];
        dfaStoreState(default_target);
    }
    
    std::vector<char> statuses(statuses_str.begin(), statuses_str.end());
    statuses.push_back('\0');
    DFA* mona_dfa = dfaBuild(statuses.data());
    
    std::vector<std::string> dfa_var_names = {"v0", "v1", "v2"};
    Syft::ExplicitStateDfa explicit_dfa(mona_dfa, dfa_var_names);
    
    std::shared_ptr<Syft::VarMgr> var_mgr = std::make_shared<Syft::VarMgr>();
    var_mgr->create_named_variables(dfa_var_names);
    var_mgr->partition_variables(dfa_var_names, {});
    Syft::ExplicitStateDfaAdd explicit_dfa_add = Syft::ExplicitStateDfaAdd::from_dfa_mona(var_mgr, explicit_dfa);
    Syft::SymbolicStateDfa symbolic_dfa = Syft::SymbolicStateDfa::from_explicit(std::move(explicit_dfa_add));
    
    // Use Naive SCC Decomposer
    INFO("Using Naive SCC Decomposer");
    std::unique_ptr<Syft::SCCDecomposer> decomposer = std::make_unique<Syft::NaiveSCCDecomposer>(symbolic_dfa);
    
    auto var_mgr_ptr = symbolic_dfa.var_mgr();
    auto automaton_id = symbolic_dfa.automaton_id();
    auto state_vars = var_mgr_ptr->get_state_variables(automaton_id);
    auto mgr = var_mgr_ptr->cudd_mgr();
    INFO("Set up completed");

    size_t num_bits = var_mgr_ptr->state_variable_count(automaton_id);
    
    CUDD::BDD all_states = mgr->bddZero();
    for (int state = 0; state < num_states; ++state) {
        all_states |= state_to_bdd(state, state_vars, var_mgr_ptr, automaton_id);
    }
    
    // Peel layers
    CUDD::BDD remaining = all_states;
    std::vector<std::set<int>> found_layers;
    
    while (!remaining.IsZero()) {
        CUDD::BDD layer = decomposer->PeelLayer(remaining);
        
        if (layer.IsZero()) {
            break;
        }
        
        std::set<int> layer_states;
        for (int state = 0; state < num_states; ++state) {
            CUDD::BDD state_bdd = state_to_bdd(state, state_vars, var_mgr_ptr, automaton_id);
            if (!(layer & state_bdd).IsZero()) {
                layer_states.insert(state);
            }
        }
        
        if (!layer_states.empty()) {
            found_layers.push_back(layer_states);
        }
        
        remaining = remaining & !layer;
    }
    
    // Compute expected SCCs using Tarjan's algorithm
    std::vector<int> scc_id(num_states, -1);
    std::vector<int> low(num_states, -1), disc(num_states, -1);
    std::vector<bool> on_stack(num_states, false);
    std::vector<int> stack;
    std::vector<std::set<int>> sccs;
    int timer = 0;
    
    std::function<void(int)> tarjan = [&](int u) {
        low[u] = disc[u] = timer++;
        stack.push_back(u);
        on_stack[u] = true;
        
        for (int v : transitions[u]) {
            if (disc[v] == -1) {
                tarjan(v);
                low[u] = std::min(low[u], low[v]);
            } else if (on_stack[v]) {
                low[u] = std::min(low[u], disc[v]);
            }
        }
        
        if (low[u] == disc[u]) {
            std::set<int> scc;
            while (true) {
                int v = stack.back();
                stack.pop_back();
                on_stack[v] = false;
                scc_id[v] = sccs.size();
                scc.insert(v);
                if (v == u) break;
            }
            sccs.push_back(scc);
        }
    };
    
    for (int i = 0; i < num_states; ++i) {
        if (disc[i] == -1) {
            tarjan(i);
        }
    }
    
    // Compute SCC graph edges
    std::set<std::pair<int,int>> scc_edges;
    for (int u = 0; u < num_states; ++u) {
        for (int v : transitions[u]) {
            if (scc_id[u] != scc_id[v]) {
                scc_edges.insert({scc_id[u], scc_id[v]});
            }
        }
    }
    
    // Compute expected layers by peeling source SCCs (indegree 0)
    std::vector<std::set<int>> expected_layers;
    std::set<int> remaining_sccs;
    for (size_t i = 0; i < sccs.size(); ++i) {
        remaining_sccs.insert(i);
    }
    
    while (!remaining_sccs.empty()) {
        std::set<int> source_sccs;
        for (int scc : remaining_sccs) {
            bool is_source = true;
            for (auto& [from, to] : scc_edges) {
                if (to == scc && remaining_sccs.count(from) && from != scc) {
                    is_source = false;
                    break;
                }
            }
            if (is_source) {
                source_sccs.insert(scc);
            }
        }
        
        std::set<int> layer_states;
        for (int scc : source_sccs) {
            for (int state : sccs[scc]) {
                layer_states.insert(state);
            }
            remaining_sccs.erase(scc);
        }
        
        if (!layer_states.empty()) {
            expected_layers.push_back(layer_states);
        }
    }
    
    INFO("Found " << found_layers.size() << " SCC layers, expected " << expected_layers.size());
    REQUIRE(found_layers.size() == expected_layers.size());
    
    for (size_t i = 0; i < expected_layers.size(); ++i) {
        INFO("Layer " << i << ": found " << found_layers[i].size() << " states, expected " << expected_layers[i].size() << " states");
        REQUIRE(found_layers[i] == expected_layers[i]);
    }
    
    INFO("SCC Decomposition Random Graph Test completed - all layers verified");
}

TEST_CASE("SCC Decomposition with Unreachable States", "[scc][unreachable]")
{
    // Create a graph where:
    // - States 0, 1, 2 are reachable from initial state 0
    // - States 3, 4, 5 are unreachable
    // - State 0 -> 1 -> 2 -> 1 (SCC: {1, 2})
    // - State 3 -> 4 -> 5 -> 4 (SCC: {4, 5}, unreachable)
    
    const std::vector<std::vector<int>> transitions = {
        {1},      // 0 -> 1
        {2},      // 1 -> 2
        {1},      // 2 -> 1 (forms SCC with 1)
        {4},      // 3 -> 4 (unreachable)
        {5},      // 4 -> 5
        {4}       // 5 -> 4 (forms SCC with 4)
    };
    const int num_states = transitions.size();
    const int num_vars = 1;
    
    int indices[1] = {0};
    dfaSetup(num_states, num_vars, indices);
    
    std::string statuses_str;
    for (int state = 0; state < num_states; ++state) {
        statuses_str += "-";
        int num_trans = transitions[state].size();
        dfaAllocExceptions(num_trans);
        for (int i = 0; i < num_trans; ++i) {
            int target = transitions[state][i];
            std::string guard_str = (i % 2 == 0) ? "0" : "1";
            std::vector<char> guard(guard_str.begin(), guard_str.end());
            guard.push_back('\0');
            dfaStoreException(target, guard.data());
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
    
    Syft::NaiveSCCDecomposer decomposer(symbolic_dfa);
    
    // Compute reachable states from initial state 0
    CUDD::BDD initial_state = state_to_bdd(0, state_vars, var_mgr_ptr, automaton_id);
    
    // Build transition relation for reachability
    auto trans_result = decomposer.BuildTransitionRelationWithPrimed();
    CUDD::BDD trans_rel = trans_result.relation;
    std::size_t primed_id = trans_result.primed_automaton_id;
    auto primed_vars = var_mgr_ptr->get_state_variables(primed_id);
    
    // Existentially quantify I/O
    CUDD::BDD io_cube = var_mgr_ptr->input_cube() * var_mgr_ptr->output_cube();
    trans_rel = trans_rel.ExistAbstract(io_cube);
    
    // Compute reachable states via fixpoint
    CUDD::BDD reachable = initial_state;
    CUDD::BDD state_cube = var_mgr_ptr->state_variables_cube(automaton_id);
    CUDD::BDD primed_cube = var_mgr_ptr->state_variables_cube(primed_id);
    
    // Build substitution primed -> unprimed
    std::size_t total_vars = var_mgr_ptr->total_variable_count();
    std::vector<CUDD::BDD> primed_to_unprimed(total_vars);
    for (std::size_t i = 0; i < total_vars; ++i) {
        primed_to_unprimed[i] = mgr->bddVar(static_cast<int>(i));
    }
    for (std::size_t i = 0; i < primed_vars.size(); ++i) {
        primed_to_unprimed[primed_vars[i].NodeReadIndex()] = state_vars[i];
    }
    
    while (true) {
        CUDD::BDD post = (reachable & trans_rel).ExistAbstract(state_cube);
        post = post.VectorCompose(primed_to_unprimed);
        CUDD::BDD new_reachable = reachable | post;
        if (new_reachable == reachable) break;
        reachable = new_reachable;
    }
    
    // Enumerate reachable states
    std::set<int> reachable_states;
    size_t num_bits = var_mgr_ptr->state_variable_count(automaton_id);
    for (int s = 0; s < num_states; ++s) {
        CUDD::BDD s_bdd = state_to_bdd(s, state_vars, var_mgr_ptr, automaton_id);
        if (!(s_bdd & reachable).IsZero()) {
            reachable_states.insert(s);
        }
    }
    
    INFO("Reachable states: ");
    for (int s : reachable_states) {
        INFO("  " << s);
    }
    
    // Expected reachable: {0, 1, 2}
    REQUIRE(reachable_states == std::set<int>{0, 1, 2});
    
    // Now peel layers from reachable states only
    std::vector<std::set<int>> found_layers;
    CUDD::BDD remaining = reachable;
    
    while (!remaining.IsZero()) {
        CUDD::BDD layer = decomposer.PeelLayer(remaining);
        
        if (layer.IsZero()) {
            INFO("PeelLayer returned empty with remaining states");
            break;
        }
        
        // Enumerate states in layer
        std::set<int> layer_states;
        for (int s = 0; s < num_states; ++s) {
            CUDD::BDD s_bdd = state_to_bdd(s, state_vars, var_mgr_ptr, automaton_id);
            if (!(s_bdd & layer).IsZero()) {
                layer_states.insert(s);
            }
        }
        
        INFO("Found layer " << found_layers.size() << ": ");
        for (int s : layer_states) {
            INFO("  " << s);
        }
        
        found_layers.push_back(layer_states);
        remaining &= !layer;
    }
    
    // Expected layers from reachable states:
    // Layer 0 (top/source): {0} - only reaches {1,2}, nothing reaches it
    // Layer 1: {1, 2} - SCC
    INFO("Found " << found_layers.size() << " layers");
    REQUIRE(found_layers.size() == 2);
    REQUIRE(found_layers[0] == std::set<int>{0});
    REQUIRE(found_layers[1] == std::set<int>{1, 2});
}
