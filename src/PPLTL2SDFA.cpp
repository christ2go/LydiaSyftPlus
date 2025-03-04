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

    std::string ppltl_formula = "Y(a) && (!b S c)";
    std::stringstream formula_stream(ppltl_formula);
    driver->parse(formula_stream);
    auto parsed_formula = driver->get_result();

    auto ppltl = std::static_pointer_cast<const whitemech::lydia::PPLTLFormula>(parsed_formula);

    // symbolic DFA construction
    auto sdfa = Syft::SymbolicStateDfa::dfa_of_ppltl_formula(*ppltl);
}