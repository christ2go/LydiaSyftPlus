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

        std::cout << "Current state: ";
        for (const auto& b : state) std::cout << b;
        std::cout << std::endl;

        std::vector<int> interpretation;
        std::string interpretation_string = "";
        interpretation.reserve(n_atoms + n_state_vars);
        for (int i = 0; i < n_atoms; ++i) {
            std::string atom_name = var_mgr->index_to_name(i);
            if (atom_name[0] == 'Y' || atom_name[0] == 'W') continue;
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

        std::cout << "Current trace: ";
        for (const auto& t : trace) std::cout << t;
        std::cout << std::endl;

        if (final_states.Eval(interpretation.data()).IsOne()) std::cout << "Current state is FINAL" << std::endl;
        else std::cout << "Current state is NOT FINAL" << std::endl;

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
        std::cout << "--------------------------------" << std::endl;
    }
}

int main(int argc, char** argv) {

    // PPLTL Driver
    std::shared_ptr<whitemech::lydia::AbstractDriver> driver;
    driver = std::make_shared<whitemech::lydia::parsers::ppltl::PPLTLDriver>();

    // std::string ppltl_formula = "(Y(a) && Y(b)) || Y(c)";
    std::string ppltl_formula = "Y(a) && (!b S c)";
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
    auto sdfa = Syft::SymbolicStateDfa::dfa_of_ppltl_formula(*ppltl);

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

    // interactive mode
    interactive(sdfa);

}