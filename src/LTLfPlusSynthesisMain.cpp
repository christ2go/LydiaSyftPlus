#include <memory>

#include "game/InputOutputPartition.h"
#include "Utils.h"
#include <lydia/logic/ltlfplus/base.hpp>
#include <lydia/logic/ltlfplus/duality.hpp>
#include <lydia/parser/ltlfplus/driver.hpp>
#include <lydia/logic/pnf.hpp>
#include "synthesizer/LTLfPlusSynthesizer.h"
#include "synthesizer/LTLfPlusSynthesizerMP.h"
#include "synthesizer/ObligationLTLfPlusSynthesizer.h"
#include "ObligationFragmentDetector.h"


#include <CLI/CLI.hpp>
#include "debug.hpp"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

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
    bool verbose = false;
    bool DEBUG_MODE = false;
    bool STRATEGY = false;
    bool obligation_simplification = false;
    std::string buechi_mode_str = "wg"; // default to weak-game (SCC) solver
    auto console = spdlog::stdout_color_mt("console");
    spdlog::set_default_logger(console);
    spdlog::set_level(spdlog::level::trace); // or debug, trace, etc.
    spdlog::set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");

    // CLI::Option* ltlf_plus_file_opt;
    app.add_option("-i,--input-file", ltlf_plus_file, "Path to LTLf+ formula file")->
            required() -> check(CLI::ExistingFile);

    // CLI::Option* partition_file_opt;
    app.add_option("-p,--partition-file", partition_file, "Path to partition file")->
            required() -> check(CLI::ExistingFile);

    // CLI::Option* starting_player_opt =
    app.add_option("-s,--starting-player", starting_player_id, "Starting player:\nagent=1;\nenvironment=0.")->
            required();

    app.add_option("-g,--game-solver", game_solver, "Game:\nManna-Pnueli-Adv=2;\nManna-Pnueli=1;\nEmerson-Lei=0.")->
            required();

    app.add_option("--obligation-simplification", obligation_simplification, "should obligation properties be treated using simpler algorithm (boolean)") ->
        required();

    app.add_option("-b,--buechi-mode", buechi_mode_str, "Solver mode: wg (weak-game / SCC), cl (Büchi classic), pm (Büchi Piterman), cb (CoBuchi)")
    ->default_val("wg");

    app.add_flag("-v,--verbose", verbose, "Enable verbose mode");      

    CLI11_PARSE(app, argc, argv);

    // parse and process input LTLf+ formula
    // read formula
    std::string ltlf_plus_formula_str;
    std::ifstream ltlf_plus_formula_stream(ltlf_plus_file);
    getline(ltlf_plus_formula_stream, ltlf_plus_formula_str);
    if (verbose) {
        std::cout << "LTLf+ formula: " << ltlf_plus_formula_str << std::endl;
    }

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

    // std::cout << "Color formula: " << pnf.color_formula_ << std::endl;

    // construct LTLfPlusSynthesizer obj
    Syft::Player starting_player;
    if (starting_player_id) {
        starting_player = Syft::Player::Agent;
    } else {
        starting_player = Syft::Player::Environment;
    }

    Syft::InputOutputPartition partition =
        Syft::InputOutputPartition::read_from_file(partition_file);

    // Use obligation synthesizer if enabled
    if (obligation_simplification) {
        std::cout << "Using obligation fragment synthesizer" << std::endl;
        try {
            // Map CLI string to use_buchi flag and BuchiMode enum
            bool use_buchi_flag = true;
            Syft::BuchiSolver::BuchiMode mode = Syft::BuchiSolver::BuchiMode::CLASSIC;

            if (buechi_mode_str == "wg" || buechi_mode_str == "weak" || buechi_mode_str == "weak-game") {
                use_buchi_flag = false; // use SCC-based weak-game solver
            } else {
                // When selecting a Büchi-based solver, set the enum appropriately
                if (buechi_mode_str == "pm" || buechi_mode_str == "piterman") {
                    mode = Syft::BuchiSolver::BuchiMode::PITERMAN;
                } else if (buechi_mode_str == "cb" || buechi_mode_str == "cobuchi") {
                    mode = Syft::BuchiSolver::BuchiMode::COBUCHI;
                } else {
                    // default to classic if unrecognised but not wg
                    mode = Syft::BuchiSolver::BuchiMode::CLASSIC;
                }
            }

            Syft::ObligationLTLfPlusSynthesizer obligation_synthesizer(
                ltlf_plus_formula,
                partition,
                starting_player,
                Syft::Player::Agent,
                use_buchi_flag,
                mode
            );
            auto synthesis_result = obligation_synthesizer.run();

            if (synthesis_result.realizability) {
                std::cout << "LTLf+ synthesis is REALIZABLE" << std::endl;
            } else {
                std::cout << "LTLf+ synthesis is UNREALIZABLE" << std::endl;
            }
        } catch (const std::runtime_error& e) {
            std::cerr << "Error: " << e.what() << std::endl;
            std::cerr << "The formula is not in the obligation fragment. Use a different synthesizer." << std::endl;
            return 1;
        }
        return 0;
    }

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
            if (verbose) {
                std::cout << "Strategy:" << std::endl;
                for (auto item : synthesis_result.output_function) {
                    std::cout << "state: " << item.gameNode;
                    item.gameNode.PrintCover();
    
                    std::cout << "tree node: " << item.t->order << "\n";
                    std::cout << " -> \n";
                    std::cout << "Y: " << item.Y;
                    item.Y.PrintCover();
                    std::cout << "tree node: " << item.u->order << "\n\n";
                }
            }
        } else {
            std::cout << "LTLf+ synthesis is UNREALIZABLE" << std::endl;
        }
    } else {
        if ((game_solver != 1) & (game_solver != 2)) {
            std::cout << "Please specify a correct game solver. \nGame:\nManna-Pnueli-Adv=2;\nManna-Pnueli=1;\nEmerson-Lei=0" << std::endl;
            return 0;
        }
            std::cout << "Using MP solvers" << std::endl;

        Syft::LTLfPlusSynthesizerMP synthesizerMP(
        ltlf_plus_formula,
        partition,
        starting_player,
        Syft::Player::Agent,
        game_solver
    );
            std::cout << "Running MP solver" << std::endl;

        auto synthesis_result_MP = synthesizerMP.run();
        if (synthesis_result_MP.realizability) {
            std::cout << "LTLf+ synthesis is REALIZABLE" << std::endl;
            if (verbose) {
                std::cout << "Strategy:" << std::endl;
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
            }
        } else {
            std::cout << "LTLf+ synthesis is UNREALIZABLE" << std::endl;
        }
    }

}