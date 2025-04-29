#include <memory>
#include "game/InputOutputPartition.h"
#include "Preprocessing.h"
#include "Utils.h"
#include <lydia/logic/ppltlplus/base.hpp>
#include <lydia/logic/ppltlplus/duality.hpp>
#include <lydia/parser/ppltlplus/driver.hpp>
#include <lydia/logic/pp_pnf.hpp>
#include "synthesizer/PPLTLfPlusSynthesizer.h"
#include "synthesizer/PPLTLfPlusSynthesizerMP.h"
#include <CLI/CLI.hpp>

int main(int argc, char** argv) {
    
    CLI::App app {
        "PLydiaSyft-EL: A compositional synthesizer of PPLTL+"
    };

    // arguments
    std::string ppltl_plus_file, partition_file;
    int starting_player_id, game_solver;

    CLI::Option* ppltl_plus_file_opt;
    app.add_option("-i,--input-file", ppltl_plus_file, "Path to PPLTL+ formula file")->
            required() -> check(CLI::ExistingFile);

    CLI::Option* partition_file_opt;
    app.add_option("-p,--partition-file", partition_file, "Path to partition file")->
            required() -> check(CLI::ExistingFile);

    CLI::Option* starting_player_opt =
        app.add_option("-s,--starting-player", starting_player_id, "Starting player:\nagent=1;\nenvironment=0.")->
            required();

    CLI::Option* game_solver_opt =
        app.add_option("-g,--game-solver", game_solver, "Game:\nManna-Pnueli=1;\nEmerson-Lei=0.") ->
        required();

    CLI11_PARSE(app, argc, argv);

    // parse and process input PPLTL+ formula
    // read formula
    std::string pplf_plus_formula_str;
    std::ifstream pplf_plus_formula_stream(ppltl_plus_file);
    getline(pplf_plus_formula_stream, pplf_plus_formula_str);
    std::cout << "PPLTL+ formula: " << pplf_plus_formula_str << std::endl;

    // PPLTL+ driver
    std::shared_ptr<whitemech::lydia::parsers::ppltlplus::PPLTLPlusDriver> driver = 
        std::make_shared<whitemech::lydia::parsers::ppltlplus::PPLTLPlusDriver>();

    // parse formula
    std::stringstream formula_stream(pplf_plus_formula_str);
    driver->parse(formula_stream);
    auto result = driver->get_result();

    // cast ast_ptr into ppltl_plus_ptr
    auto ppltl_plus_ptr = 
        std::static_pointer_cast<const whitemech::lydia::PPLTLPlusFormula>(result);

    // transform formula in PNF
    auto pnf = whitemech::lydia::get_pnf_result(*ppltl_plus_ptr);
    Syft::PPLTLPlus ppltl_plus_formula;
    ppltl_plus_formula.color_formula_ = pnf.color_formula_;
    ppltl_plus_formula.formula_to_color_= pnf.subformula_to_color_;
    ppltl_plus_formula.formula_to_quantification_= pnf.subformula_to_quantifier_;

    // debug
    for (const auto& [formula, color] : pnf.subformula_to_color_) {
        std::cout << "PPLTL+ Formula: " << whitemech::lydia::to_string(*formula) << ". Color: " << color << std::flush;
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

    // construct PPLTLPlusSynthesizer obj
    Syft::Player starting_player;
    if (starting_player_id) {
        starting_player = Syft::Player::Agent;
    } else {
        starting_player = Syft::Player::Environment;
    }
    Syft::InputOutputPartition partition =
        Syft::InputOutputPartition::read_from_file(partition_file);

    // show result
    if (game_solver == 0) 
    {
        Syft::PPLTLfPlusSynthesizer synthesizer(
            ppltl_plus_formula,
            partition,
            starting_player,
            Syft::Player::Agent);
    
        // do synthesis
        auto synthesis_result = synthesizer.run();

        if (synthesis_result.realizability) {
            std::cout << "PPLTL+ synthesis is REALIZABLE" << std::endl;

            for (auto item : synthesis_result.output_function) {
                std::cout << "state: " << item.gameNode;
                item.gameNode.PrintCover();

                std::cout << "tree node: " << item.t->order << "\n";
                std::cout << " -> \n";
                std::cout << "Y: " << item.Y;
                item.Y.PrintCover();
                std::cout << "tree node: " << item.u->order << "\n\n";
            }
        } else std::cout << "PPLTL+ synthesis is UNREALIZABLE" << std::endl;
    } else {
        Syft::PPLTLfPlusSynthesizerMP synthesizerMP(
            ppltl_plus_formula,
            partition,
            starting_player,
            Syft::Player::Agent
        );

        auto synthesis_result_MP = synthesizerMP.run();
        if (synthesis_result_MP.realizability) {
            std::cout << "PPLTL+ synthesis is REALIZABLE" << std::endl;
        } else {
            std::cout << "PPLTL+ synthesis is UNREALIZABLE" << std::endl;
        }
    }
}