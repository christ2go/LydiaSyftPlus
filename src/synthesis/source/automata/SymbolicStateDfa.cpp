#include "automata/SymbolicStateDfa.h"

namespace Syft {

    SymbolicStateDfa::SymbolicStateDfa(std::shared_ptr<VarMgr> var_mgr)
            : var_mgr_(std::move(var_mgr)) {}

    std::pair<std::size_t, std::size_t> SymbolicStateDfa::create_state_variables(
            std::shared_ptr<VarMgr> &var_mgr,
            std::size_t state_count) {
        // Largest state index that needs to be represented
        std::size_t max_state = state_count - 1;

        std::size_t bit_count = 0;

        // Number of iterations equals the log of the state count
        while (max_state > 0) {
            ++bit_count;
            max_state >>= 1;
        }

        std::size_t automaton_id = var_mgr->create_state_variables(bit_count);

        return std::make_pair(bit_count, automaton_id);
    }

    std::vector<int> SymbolicStateDfa::state_to_binary(std::size_t state,
                                                       std::size_t bit_count) {
        std::vector<int> binary_representation;

        while (state != 0) {
            // Add the least significant bit of the state to the binary representation
            binary_representation.push_back(state & 1);

            // Shift right
            state >>= 1;
        }

        // Fill rest of the vector with zeroes up to bit_count
        binary_representation.resize(bit_count);

        // Note that the binary representation goes from least to most significant bit

        return binary_representation;
    }

    CUDD::BDD SymbolicStateDfa::state_to_bdd(
            const std::shared_ptr<VarMgr> &var_mgr,
            std::size_t automaton_id,
            std::size_t state) {
        std::size_t bit_count = var_mgr->state_variable_count(automaton_id);
        std::vector<int> binary_representation = state_to_binary(state, bit_count);

        return var_mgr->state_vector_to_bdd(automaton_id, binary_representation);
    }

    CUDD::BDD SymbolicStateDfa::state_set_to_bdd(
            const std::shared_ptr<VarMgr> &var_mgr,
            std::size_t automaton_id,
            const std::vector<size_t> &states) {
        CUDD::BDD bdd = var_mgr->cudd_mgr()->bddZero();

        for (std::size_t state: states) {
            bdd |= state_to_bdd(var_mgr, automaton_id, state);
        }

        return bdd;
    }

    std::vector<CUDD::BDD> SymbolicStateDfa::symbolic_transition_function(
            const std::shared_ptr<VarMgr> &var_mgr,
            std::size_t automaton_id,
            const std::vector<CUDD::ADD> &transition_function) {
        std::size_t bit_count = var_mgr->state_variable_count(automaton_id);
        std::vector<CUDD::BDD> symbolic_transition_function(
                bit_count, var_mgr->cudd_mgr()->bddZero());

        for (std::size_t j = 0; j < transition_function.size(); ++j) {
            CUDD::BDD state_bdd = state_to_bdd(var_mgr, automaton_id, j);

            for (std::size_t i = 0; i < bit_count; ++i) {
                // BddIthBit counts from the least-significant bit
                CUDD::BDD jth_component = state_bdd & transition_function[j].BddIthBit(i);

                symbolic_transition_function[i] |= jth_component;
            }
        }

        return symbolic_transition_function;
    }

    SymbolicStateDfa SymbolicStateDfa::from_explicit(
            const ExplicitStateDfaAdd &explicit_dfa) {
        std::shared_ptr<VarMgr> var_mgr = explicit_dfa.var_mgr();

        auto count_and_id = create_state_variables(var_mgr,
                                                   explicit_dfa.state_count());
        std::size_t bit_count = count_and_id.first;
        std::size_t automaton_id = count_and_id.second;

        std::vector<int> initial_state = state_to_binary(explicit_dfa.initial_state(),
                                                         bit_count);

        CUDD::BDD final_states = state_set_to_bdd(var_mgr, automaton_id,
                                                  explicit_dfa.final_states());

        std::vector<CUDD::BDD> transition_function = symbolic_transition_function(
                var_mgr, automaton_id, explicit_dfa.transition_function());

        SymbolicStateDfa symbolic_dfa(var_mgr);
        symbolic_dfa.automaton_id_ = automaton_id;
        symbolic_dfa.initial_state_ = std::move(initial_state);
        symbolic_dfa.final_states_ = std::move(final_states);
        symbolic_dfa.transition_function_ = std::move(transition_function);

        return symbolic_dfa;
    }

    std::shared_ptr<VarMgr> SymbolicStateDfa::var_mgr() const {
        return var_mgr_;
    }

    std::size_t SymbolicStateDfa::automaton_id() const {
        return automaton_id_;
    }

    std::vector<int> SymbolicStateDfa::initial_state() const {
        return initial_state_;
    }

    CUDD::BDD SymbolicStateDfa::initial_state_bdd() const {
        auto sv = var_mgr()->get_state_variables(automaton_id_);
        auto is = initial_state();
        CUDD::BDD bdd = var_mgr()->cudd_mgr()->bddOne();
        for (int i = 0; i < is.size(); ++i) {
            if (is[i]) bdd *= sv[i];
            else if (!is[i]) bdd *= !sv[i];
        }
        return bdd;
        // return state_to_bdd(var_mgr_, automaton_id_, 1);
    }

    CUDD::BDD SymbolicStateDfa::final_states() const {
        return final_states_;
    }

    std::vector<CUDD::BDD> SymbolicStateDfa::transition_function() const {
        return transition_function_;
    }

    void SymbolicStateDfa::restrict_dfa_with_states(const CUDD::BDD &valid_states) {
        for (CUDD::BDD &bit_function: transition_function_) {
            // If the current state is not a valid state, send every transition to
            // the sink state 0
            bit_function &= valid_states;
        }

        // Restrict the set of accepting states to valid states
        final_states_ &= valid_states;
    }

    void SymbolicStateDfa::restrict_dfa_with_transitions(const CUDD::BDD &feasible_moves) {
        for (CUDD::BDD &bit_function: transition_function_) {
            // Every transition has to be a feasible move
            bit_function &= feasible_moves;
        }
    }


    void SymbolicStateDfa::dump_dot(const std::string &filename) const {
        std::vector<std::string> function_labels =
                var_mgr_->state_variable_labels(automaton_id_);
        function_labels.push_back("Final");

        std::vector<CUDD::ADD> adds;
        adds.reserve(transition_function_.size() + 1);

        for (const CUDD::BDD &bdd: transition_function_) {
            adds.push_back(bdd.Add());
        }

        adds.push_back(final_states_.Add());

        var_mgr_->dump_dot(adds, function_labels, filename);
    }

    SymbolicStateDfa SymbolicStateDfa::from_predicates(
            std::shared_ptr<VarMgr> var_mgr,
            std::vector<CUDD::BDD> predicates) {
        std::size_t predicate_count = predicates.size();
        std::vector<int> initial_state(predicate_count, 0);
        CUDD::BDD final_states = var_mgr->cudd_mgr()->bddOne();
        std::size_t automaton_id = var_mgr->create_state_variables(predicate_count);

        SymbolicStateDfa dfa(std::move(var_mgr));
        dfa.automaton_id_ = automaton_id;
        dfa.initial_state_ = std::move(initial_state);
        dfa.transition_function_ = std::move(predicates);
        dfa.final_states_ = std::move(final_states);

        return dfa;
    }

    SymbolicStateDfa SymbolicStateDfa::product_AND(const std::vector<SymbolicStateDfa> &dfa_vector) {
        if (dfa_vector.size() < 1) {
            throw std::runtime_error("Incorrect usage of automata product");
        }

        std::shared_ptr<VarMgr> var_mgr = dfa_vector[0].var_mgr();

        std::vector<std::size_t> automaton_ids;

        std::vector<int> initial_state;

        CUDD::BDD final_states = var_mgr->cudd_mgr()->bddOne();
        std::vector<CUDD::BDD> transition_function;

        for (SymbolicStateDfa dfa: dfa_vector) {
            automaton_ids.push_back(dfa.automaton_id());

            std::vector<int> dfa_initial_state = dfa.initial_state();
            initial_state.insert(initial_state.end(), dfa_initial_state.begin(), dfa_initial_state.end());

            final_states = final_states & dfa.final_states();
            std::vector<CUDD::BDD> dfa_transition_function = dfa.transition_function();
            transition_function.insert(transition_function.end(), dfa_transition_function.begin(),
                                       dfa_transition_function.end());
        }

        std::size_t product_automaton_id = var_mgr->create_product_state_space(automaton_ids);

        SymbolicStateDfa product_automaton(var_mgr);
        product_automaton.automaton_id_ = product_automaton_id;
        product_automaton.initial_state_ = std::move(initial_state);
        product_automaton.final_states_ = std::move(final_states);
        product_automaton.transition_function_ = std::move(transition_function);

        return product_automaton;
    }

    void SymbolicStateDfa::new_sink_states(const CUDD::BDD &states) {
        int i = 0;
        while (i < transition_function_.size()) {
            CUDD::BDD bit_function = transition_function_[i];
            CUDD::BDD bit = var_mgr()->state_variable(automaton_id_, i);
//        var_mgr()->dump_dot(bit.Add(), "bit"+std::to_string(i));
            CUDD::BDD new_bit_function = (bit_function & !states) | (states * bit);
            transition_function_[i] = new_bit_function;
            i++;
        }
    }

    SymbolicStateDfa SymbolicStateDfa::product_OR(const std::vector<SymbolicStateDfa> &dfa_vector) {
        if (dfa_vector.size() < 1) {
            throw std::runtime_error("Incorrect usage of automata union");
        }

        std::shared_ptr<VarMgr> var_mgr = dfa_vector[0].var_mgr();

        std::vector<std::size_t> automaton_ids;

        std::vector<int> initial_state;

        CUDD::BDD final_states = var_mgr->cudd_mgr()->bddZero();
        std::vector<CUDD::BDD> transition_function;

        for (SymbolicStateDfa dfa: dfa_vector) {
            automaton_ids.push_back(dfa.automaton_id());

            std::vector<int> dfa_initial_state = dfa.initial_state();
            initial_state.insert(initial_state.end(), dfa_initial_state.begin(), dfa_initial_state.end());

            final_states = final_states | dfa.final_states();
            std::vector<CUDD::BDD> dfa_transition_function = dfa.transition_function();
            transition_function.insert(transition_function.end(), dfa_transition_function.begin(),
                                       dfa_transition_function.end());
        }

        std::size_t union_automaton_id = var_mgr->create_product_state_space(automaton_ids);

        SymbolicStateDfa product_automaton(var_mgr);
        product_automaton.automaton_id_ = union_automaton_id;
        product_automaton.initial_state_ = std::move(initial_state);
        product_automaton.final_states_ = std::move(final_states);
        product_automaton.transition_function_ = std::move(transition_function);

        return product_automaton;
    }


    SymbolicStateDfa SymbolicStateDfa::complement(const SymbolicStateDfa dfa) {
        std::shared_ptr<VarMgr> var_mgr = dfa.var_mgr();

        std::size_t complement_automaton_id = var_mgr->create_complement_state_space(dfa.automaton_id());

        std::vector<int> initial_state = dfa.initial_state();

        CUDD::BDD final_states = !dfa.final_states();


        SymbolicStateDfa complement_automaton(std::move(var_mgr));
        complement_automaton.automaton_id_ = complement_automaton_id;
        complement_automaton.initial_state_ = std::move(initial_state);
        complement_automaton.transition_function_ = std::move(dfa.transition_function());
        complement_automaton.final_states_ = std::move(final_states);

        return complement_automaton;
    }

    SymbolicStateDfa SymbolicStateDfa::dfa_of_ppltl_formula(const whitemech::lydia::PPLTLFormula& formula) {
        
        std::shared_ptr<VarMgr> mgr = std::make_shared<VarMgr>();

        whitemech::lydia::StrPrinter p;

        // get NNF
        whitemech::lydia::NNFTransformer t;
        auto nnf = t.apply(formula);

        // get YNF
        whitemech::lydia::YNFTransformer yt;
        auto ynf = yt.apply(*nnf);

        // get subformulas
        auto y_sub = yt.get_y_sub();
        auto wy_sub = yt.get_wy_sub();
        auto atoms  = yt.get_atoms();

        // creates Boolean variables for the atoms
        std::vector<std::string> str_atoms;
        for (const auto& a : atoms) str_atoms.push_back(p.apply(*a));
        mgr->create_named_variables(str_atoms);

        // creates Boolean variables for Y and WY subformulas
        std::vector<std::string> str_sub;
        str_sub.reserve(y_sub.size() + wy_sub.size() + 1);
        for (const auto& a : y_sub) str_sub.push_back(p.apply(*a));
        for (const auto& a : wy_sub) str_sub.push_back(p.apply(*a));
        auto val_str = "VAL"+std::to_string(mgr->automaton_num());
        str_sub.push_back(val_str);
        auto dfa_id = mgr->create_named_state_variables(str_sub);

        // transition function and initial state
        // Z = (Y0, ..., Yn, WY0, ...., WYm) {0, 1}
        // d = (dY0, ..., dYn, dWY0, ..., dWYm) {BDD}
        std::vector<CUDD::BDD> transition_function;
        transition_function.reserve(y_sub.size() + wy_sub.size() + 1);
        std::vector<int> init_state;
        init_state.reserve(y_sub.size() + wy_sub.size() + 1);
        ValVisitor v(mgr);

        // Y state vars
        for (const auto& f : y_sub) {
            auto ya = std::static_pointer_cast<const whitemech::lydia::PPLTLYesterday>(f);
            auto arg = ya->get_arg();
            auto bdd = val(*arg, mgr);
            transition_function.push_back(bdd);
            init_state.push_back(0);
        }
        // WY state vars
        for (const auto& f : wy_sub) {
            auto wya = std::static_pointer_cast<const whitemech::lydia::PPLTLWeakYesterday>(f);
            auto arg = wya->get_arg();
            auto bdd = val(*arg, mgr);
            transition_function.push_back(bdd);
            init_state.push_back(1);
        }
        // final state var
        transition_function.push_back(val(*ynf, mgr));
        init_state.push_back(0);

        // final states
        CUDD::BDD final_states = mgr->name_to_variable(val_str);

        // output 
        SymbolicStateDfa dfa(std::move(mgr));
        dfa.automaton_id_ = dfa_id;
        dfa.initial_state_ = std::move(init_state);
        dfa.transition_function_ = std::move(transition_function);
        dfa.final_states_ = std::move(final_states);

        return dfa;
    }

    SymbolicStateDfa SymbolicStateDfa::get_exists_dfa(const SymbolicStateDfa& sdfa) {
        auto mgr = sdfa.var_mgr();
        auto edfa_id = mgr->copy_state_space(sdfa.automaton_id());
        auto val_str = "VAL"+std::to_string(sdfa.automaton_id());
        auto val_var = mgr->name_to_variable(val_str);
        auto transition_function = sdfa.transition_function();
        auto final_states = sdfa.final_states();
        auto lst = transition_function.size() - 1;

        // transform final states into sinks
        // transition_function[lst] is the transition function of val_var
        // we modify such transtion function as follows:
        // in the next time step val_var evaluates to 1 iff 
        // val or val_var hold in the current time step
        // it follows that, if val_var evaluates to 1 once
        // i.e., a final state has been visited
        // val_var will evaluate to 1 forever
        transition_function[lst] = transition_function[lst] + val_var;

        // remove initial state from final states
        auto init_state_bdd = sdfa.initial_state_bdd();
        auto new_final_states = final_states * !init_state_bdd;

        SymbolicStateDfa edfa(std::move(mgr));
        edfa.automaton_id_ = edfa_id; 
        edfa.initial_state_ = sdfa.initial_state(); // same initial state
        edfa.transition_function_ = std::move(transition_function);        
        edfa.final_states_ = std::move(new_final_states);

        return edfa;
    }

    SymbolicStateDfa SymbolicStateDfa::get_forall_dfa(const SymbolicStateDfa& sdfa) {
        auto mgr = sdfa.var_mgr();
        auto adfa_id = mgr->copy_state_space(sdfa.automaton_id());
        auto val_str = "VAL"+std::to_string(sdfa.automaton_id());
        auto val_var = mgr->name_to_variable(val_str);
        auto transition_function = sdfa.transition_function();
        auto final_states = sdfa.final_states();
        auto lst = transition_function.size() - 1;

        // transform non-final states into sinks
        // transition_function[lst] is the transition function of val_var
        // we modify such transtion function as follows:
        // in the next time step val_var evaluates to 1 iff
        // val and val_var hold in the current time step
        // it follows that, if val_var evaluates to 0 once
        // i.e., a non-final state has been visited
        // then it will evaluate to 0 forever
        transition_function[lst] = transition_function[lst] * val_var;

        auto new_init_state = sdfa.initial_state();
        new_init_state[lst] = 1;

        SymbolicStateDfa adfa(std::move(mgr));
        adfa.automaton_id_ = adfa_id;
        adfa.initial_state_ = std::move(new_init_state);
        adfa.transition_function_ = std::move(transition_function);

        // remove initial state from final states
        auto init_state_bdd = adfa.initial_state_bdd();
        auto new_final_states = final_states * !init_state_bdd;

        adfa.final_states_ = std::move(new_final_states);

        return adfa;
    }
}

