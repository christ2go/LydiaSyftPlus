#include <memory>

#include "Preprocessing.h"
#include "Utils.h"
#include <lydia/logic/ppltl/base.hpp>
#include <lydia/parser/ppltl/driver.hpp>
#include <lydia/logic/nnf.hpp>
#include <lydia/logic/ynf.hpp>
#include "automata/SymbolicStateDfa.h"
#include "automata/ppltl/ValVisitor.h"

int main(int argc, char** argv) {

    // PPLTL Driver
    std::shared_ptr<whitemech::lydia::AbstractDriver> driver;
    driver = std::make_shared<whitemech::lydia::parsers::ppltl::PPLTLDriver>();

    // std::string ppltl_formula = "(Y(a) && Y(b)) || Y(c)";
    std::string ppltl_formula = "Y(a) || Y(b) || Y(c)";
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

}