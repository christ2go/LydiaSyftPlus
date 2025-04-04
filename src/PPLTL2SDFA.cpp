#include <memory>

#include "Preprocessing.h"
#include "Utils.h"
#include <lydia/logic/ppltl/base.hpp>
#include <lydia/parser/ppltl/driver.hpp>
#include <lydia/logic/nnf.hpp>
#include <lydia/logic/ynf.hpp>
#include "automata/SymbolicStateDfa.h"
#include "automata/ppltl/ValVisitor.h"

void interactive(const Syft::SymbolicStateDfa& d) {
    auto state = d.initial_state();
    auto transition_function = d.transition_function();
    auto final_states = d.final_states();
    auto var_mgr = d.var_mgr();
    auto n_state_vars = var_mgr->total_state_variable_count();
    auto n_atoms = var_mgr->total_variable_count() - n_state_vars;

    std::vector<std::string> trace;
    while (true) {
        std::cout << "--------------------------------" << std::endl;

        std::cout << "Current trace: ";
        for (const auto& t : trace) std::cout << t;
        std::cout << std::endl;

        std::cout << "Current state: ";
        for (const auto& b : state) std::cout << b;
        std::cout << std::endl;

        std::vector<int> state_eval;
        state_eval.reserve(n_atoms + n_state_vars);
        for (int i = 0; i < n_atoms ; ++i) {
            std::string atom_name = var_mgr->index_to_name(i);
            if (!(atom_name[0] == 'Y' || atom_name[0] == 'W' || atom_name[0] == 'V' || atom_name[0] == 'N')) state_eval.push_back(0);
        }
        for (const auto& b : state) state_eval.push_back(b);

        // print state_eval
        std::cout << "State evaluation: ";
        for (const auto& i : state_eval) std::cout << i;
        std::cout << ". Size: " << state_eval.size() << std::endl;
        std::cout << std::endl;

        if (final_states.Eval(state_eval.data()).IsOne()) std::cout << "Current state is FINAL" << std::endl;
        else std::cout << "Current state is NOT FINAL" << std::endl;

        std::vector<int> interpretation;
        std::string interpretation_string = "";
        interpretation.reserve(n_atoms + n_state_vars);
        for (int i = 0; i < n_atoms; ++i) {
            std::string atom_name = var_mgr->index_to_name(i);
            if (atom_name[0] == 'Y' || atom_name[0] == 'W' || atom_name[0] == 'V' || atom_name[0] == 'N') continue;
            std::cout << "Enter value for atom " << atom_name << ": ";
            int value;
            std::cin >> value;
            if (value != 0 && value != 1) throw std::runtime_error("Invalid value for atom");
            if (value == 1) interpretation_string += atom_name + ",";
            interpretation.push_back(value);
        }
        interpretation_string = "{" + interpretation_string.substr(0, interpretation_string.size() - 1) + "}";
        for (const auto& b : state) interpretation.push_back(b);

        trace.push_back(interpretation_string);

        std::cout << "Interpretation: ";
        for (const auto& i : interpretation) std::cout << i;
        std::cout << std::endl;

        std::vector<int> new_state;
        new_state.reserve(state.size());

        for (const auto& bdd : transition_function) {
            auto eval = bdd.Eval(interpretation.data()).IsOne();
            new_state.push_back(eval);
        }
        state = new_state;

        std::string exit;
        std::cout << "Do you want to exit interactive mode? (y/n): ";
        std::cin >> exit;
        if (exit == "y") return;
    
        std::cout << "--------------------------------" << std::endl;
    }
}

int main(int argc, char** argv) {

    // PPLTL Driver
    std::shared_ptr<whitemech::lydia::AbstractDriver> driver;
    driver = std::make_shared<whitemech::lydia::parsers::ppltl::PPLTLDriver>();

    std::string ppltl_formula;
    std::cout << "Enter a PPLTL formula: ";
    std::getline(std::cin, ppltl_formula);
    
    std::stringstream formula_stream(ppltl_formula);
    driver->parse(formula_stream);
    auto parsed_formula = driver->get_result();

    auto ppltl = std::static_pointer_cast<const whitemech::lydia::PPLTLFormula>(parsed_formula);

    // get NNF
    whitemech::lydia::NNFTransformer t;
    auto nnf = t.apply(*ppltl);

    // get YNF
    whitemech::lydia::YNFTransformer yt;
    auto ynf = yt.apply(*nnf);

    whitemech::lydia::StrPrinter p;
    auto s_ynf = p.apply(*ynf);

    std::cout << "Input PPLTL formula: " << ppltl_formula << std::endl;
    std::cout << "YNF: " << s_ynf << std::endl;

    // symbolic DFA construction
    std::shared_ptr<Syft::VarMgr> var_mgr = std::make_shared<Syft::VarMgr>();
    std::shared_ptr<Syft::VarMgr> var_mgr2 = std::make_shared<Syft::VarMgr>();

    auto sdfa = Syft::SymbolicStateDfa::dfa_of_ppltl_formula(*ppltl, var_mgr);
    auto edfa = Syft::SymbolicStateDfa::get_exists_dfa(sdfa);
    auto adfa = Syft::SymbolicStateDfa::get_forall_dfa(sdfa);
    auto adfa_no_loops = Syft::SymbolicStateDfa::dfa_of_ppltl_formula_remove_initial_self_loops(*ppltl, var_mgr2);

    // print alphabet and state variables
    sdfa.var_mgr()->print_mgr();

    // initial state
    auto init_state = sdfa.initial_state();
    std::cout << "Initial state: " << std::flush;
    for (const auto& b : init_state) std::cout << b;
    std::cout << std::endl;

    // transition function
    auto transition_function = sdfa.transition_function();
    std::cout << "Transition function: " << std::endl;
    for (const auto& bdd : transition_function) std::cout << bdd << std::endl;

    // final states
    auto final_states = sdfa.final_states();
    std::cout << "Final states: " << final_states;
    std::cout  << std::endl;

    std::string interactive_mode;
    std::cout << "Do you want to enter interactive mode? (y/n): ";
    std::cin >> interactive_mode;
    if (interactive_mode == "y") interactive(sdfa);    

    std::cout << "--------------------------------" << std::endl;
    std::cout << "E(dfa) initial state: ";
    for (const auto& b : edfa.initial_state()) std::cout << b;
    std::cout << std::endl;

    std::cout << "E(dfa) transition function: " << std::endl;
    for (const auto& bdd : edfa.transition_function()) std::cout << bdd << std::endl;

    std::cout << "E(dfa) final states: " << edfa.final_states() << std::endl;

    std::string interactive_emode;
    std::cout << "Do you want to enter interactive mode for E(ppltl)? (y/n): ";
    std::cin >> interactive_emode;
    if (interactive_emode == "y") interactive(edfa);
    std::cout << "--------------------------------" << std::endl;

    std::cout << "--------------------------------" << std::endl;
    std::cout << "A(dfa) initial state: ";
    for (const auto& b : adfa.initial_state()) std::cout << b;
    std::cout << std::endl;

    std::cout << "A(dfa) transition function: " << std::endl;
    for (const auto& bdd : adfa.transition_function()) std::cout << bdd << std::endl;

    std::cout << "A(dfa) final states: " << adfa.final_states() << std::endl;

    std::string interactive_amode;
    std::cout << "Do you want to enter interactive mode for A(ppltl)? (y/n): ";
    std::cin >> interactive_amode;
    if (interactive_amode == "y") interactive(adfa);
    std::cout << "--------------------------------" << std::endl;

    // do the same for A'(dfa), where A' is obtained using dfa_of_ppltl_formula_remove_initial_self_loops
    var_mgr2->print_mgr();
    std::cout << "--------------------------------" << std::endl;
    std::cout << "A'(dfa) initial state: ";
    for (const auto& b : adfa_no_loops.initial_state()) std::cout << b;
    std::cout << std::endl;

    std::cout << "A'(dfa) transition function: " << std::endl;
    for (const auto& bdd : adfa_no_loops.transition_function()) std::cout << bdd << std::endl;

    std::cout << "A'(dfa) final states: " << adfa_no_loops.final_states() << std::endl;

    std::string interactive_amode_no_loops;
    std::cout << "Do you want to enter interactive mode for A'(ppltl)? (y/n): ";
    std::cin >> interactive_amode_no_loops;
    if (interactive_amode_no_loops == "y") interactive(adfa_no_loops);
    std::cout << "--------------------------------" << std::endl;
}