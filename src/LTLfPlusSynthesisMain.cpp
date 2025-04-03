#include <memory>

#include "game/InputOutputPartition.h"
#include "Utils.h"
#include <lydia/logic/ltlfplus/base.hpp>
#include <lydia/logic/ltlfplus/duality.hpp>
#include <lydia/parser/ltlfplus/driver.hpp>
#include <lydia/logic/pnf.hpp>
#include "synthesizer/LTLfPlusSynthesizer.h"
#include "synthesizer/LTLfPlusSynthesizerMP.h"
#include <CLI/CLI.hpp>

int main(int argc, char** argv) {

    CLI::App app {
        "LydiaSyft-EL: A compositional synthesizer of LTLf+"
    };

    // arguments
    // TODO (Daniel, Gianmarco, Shufang). EL synthesis only solves realizability
    // bool print_strategy = false;
    // app.add_flag("-p,--print-strategy", print_strategy, "Print out the synthesized strategy (default: false)");

    // TODO (Daniel, Gianmarco, Shufang). Add instructions to print running times
    // bool print_time = false;
    // app.add_flag("-t,--print-time", print_times, "Print running times of each step (default: false)");

    std::string ltlf_plus_file, partition_file;
    int starting_player_id, game_solver;

    // CLI::Option* ltlf_plus_file_opt;
    app.add_option("-i,--input-file", ltlf_plus_file, "Path to LTLf+ formula file")->
            required() -> check(CLI::ExistingFile);

    // CLI::Option* partition_file_opt;
    app.add_option("-p,--partition-file", partition_file, "Path to partition file")->
            required() -> check(CLI::ExistingFile);

    // CLI::Option* starting_player_opt =
    app.add_option("-s,--starting-player", starting_player_id, "Starting player:\nagent=1;\nenvironment=0.")->
            required();

    app.add_option("-g,--game-solver", game_solver, "Game:\nManna-Pnueli=1;\nEmerson-Lei=0.")->
            required();

    CLI11_PARSE(app, argc, argv);

    // parse and process input LTLf+ formula
    // read formula
    std::string ltlf_plus_formula_str;
    std::ifstream ltlf_plus_formula_stream(ltlf_plus_file);
    getline(ltlf_plus_formula_stream, ltlf_plus_formula_str);
    std::cout << "LTLf+ formula: " << ltlf_plus_formula_str << std::endl;

    // LTLf+ driver
    std::shared_ptr<whitemech::lydia::parsers::ltlfplus::LTLfPlusDriver> driver = 
        std::make_shared<whitemech::lydia::parsers::ltlfplus::LTLfPlusDriver>();

    // parse formula
    std::stringstream formula_stream(ltlf_plus_formula_str);
    driver->parse(formula_stream);
    auto result = driver->get_result();

    // cast ast_ptr into ltlf_plus_ptr. Necessary since AbstractDriver is not template anymore
    auto ptr_ltlf_plus_formula =
        std::static_pointer_cast<const whitemech::lydia::LTLfPlusFormula>(result);

    // transform formula in PNF
    auto pnf = whitemech::lydia::get_pnf_result(*ptr_ltlf_plus_formula);
    Syft::LTLfPlus ltlf_plus_formula;
    ltlf_plus_formula.color_formula_ = pnf.color_formula_;
    ltlf_plus_formula.formula_to_color_= pnf.subformula_to_color_;
    ltlf_plus_formula.formula_to_quantification_= pnf.subformula_to_quantifier_;

    // debug
    for (const auto& [formula, color] : pnf.subformula_to_color_) {
        std::cout << "LTLf+ Formula: " << whitemech::lydia::to_string(*formula) << ". Color: " << color << std::flush;
        switch (pnf.subformula_to_quantifier_[formula]) {
            case whitemech::lydia::PrefixQuantifier::ForallExists: {
                std::cout << ". Prefix Quantifier: AE" << std::endl;
                break;}
            case whitemech::lydia::PrefixQuantifier::ExistsForall: {
                std::cout << ". Prefix Quantifier: EA" << std::endl;
                break;}
            case whitemech::lydia::PrefixQuantifier::Forall: {
                std::cout << ". Prefix Quantifier: A" << std::endl;
                break;}
            case whitemech::lydia::PrefixQuantifier::Exists: {
                std::cout << ". Prefix Quantifier: E" << std::endl;
                break;}
        }
    }
    std::cout << "Color formula: " << pnf.color_formula_ << std::endl;

    // construct LTLfPlusSynthesizer obj
    Syft::Player starting_player;
    if (starting_player_id) {
        starting_player == Syft::Player::Agent;
    } else {
        starting_player == Syft::Player::Environment;
    }

    Syft::InputOutputPartition partition =
        Syft::InputOutputPartition::read_from_file(partition_file);

    if (game_solver == 0) {
        Syft::LTLfPlusSynthesizer synthesizer(
            ltlf_plus_formula,
            partition,
            starting_player,
            Syft::Player::Agent
        );
        auto synthesis_result = synthesizer.run();

        if (synthesis_result.realizability) {
            std::cout << "LTLf+ synthesis is REALIZABLE" << std::endl;
            for (auto item : synthesis_result.output_function) {
                std::cout << "state: " << item.gameNode;
                item.gameNode.PrintCover();

                std::cout << "tree node: " << item.t->order << "\n";
                std::cout << " -> \n";
                std::cout << "Y: " << item.Y;
                item.Y.PrintCover();
                std::cout << "tree node: " << item.u->order << "\n\n";
            }
        } else {
            std::cout << "LTLf+ synthesis is UNREALIZABLE" << std::endl;
        }
    } else {
        Syft::LTLfPlusSynthesizerMP synthesizerMP(
        ltlf_plus_formula,
        partition,
        starting_player,
        Syft::Player::Agent
    );

        auto synthesis_result_MP = synthesizerMP.run();
        if (synthesis_result_MP.realizability) {
            std::cout << "LTLf+ synthesis is REALIZABLE" << std::endl;
            for (auto item : synthesis_result_MP.output_function) {
                std::cout << "state: " << item.gameNode;
                item.gameNode.PrintCover();
                std::cout << "dag node: " << item.currDagNodeId << "\n";
                std::cout << "tree node: " << item.t->order << "\n";
                std::cout << " -> \n";
                std::cout << "Y: " << item.Y;
                item.Y.PrintCover();
                std::cout << "dag node: " << item.newDagNodeId << "\n";
                std::cout << "tree node: " << item.u->order << "\n\n";
            }
        } else {
            std::cout << "LTLf+ synthesis is UNREALIZABLE" << std::endl;
        }
    }

}