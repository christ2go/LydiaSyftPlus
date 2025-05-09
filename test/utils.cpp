#include "game/InputOutputPartition.h"
#include "Utils.h"
#include <lydia/logic/ltlfplus/base.hpp>
#include <lydia/logic/ltlfplus/duality.hpp>
#include <lydia/parser/ltlfplus/driver.hpp>
#include <lydia/logic/pnf.hpp>
#include "synthesizer/LTLfPlusSynthesizer.h"
#include "synthesizer/LTLfPlusSynthesizerMP.h"

#include <lydia/logic/ppltlplus/base.hpp>
#include <lydia/logic/ppltlplus/duality.hpp>
#include <lydia/parser/ppltlplus/driver.hpp>
#include <lydia/logic/pp_pnf.hpp>
#include "synthesizer/PPLTLfPlusSynthesizer.h"
#include "synthesizer/PPLTLfPlusSynthesizerMP.h"

namespace Syft
{
    namespace Test
    {

        // bool get_realizability_from_input(const std::string &formula, const std::vector<std::string> &input_variables,
        //                                   const std::vector<std::string> &output_variables)
        // {
        //     auto driver = std::make_shared<whitemech::lydia::parsers::ltlf::LTLfDriver>();
        //     auto parsed_formula = Syft::Test::parse_formula(formula, *driver);

        //     Syft::InputOutputPartition partition = Syft::InputOutputPartition::construct_from_input(input_variables,
        //                                                                                             output_variables);

        //     return get_realizability(parsed_formula, partition);
        // }

        // bool get_realizability(const whitemech::lydia::ltlf_ptr &formula, const Syft::InputOutputPartition &partition)
        // {
        //     std::shared_ptr<Syft::VarMgr> var_mgr = std::make_shared<Syft::VarMgr>();
        //     var_mgr->create_named_variables(partition.input_variables);
        //     var_mgr->create_named_variables(partition.output_variables);

        //     auto one_step_result = Syft::preprocessing(*formula, partition, *var_mgr, Player::Agent);
        //     bool preprocessing_success = one_step_result.realizability.has_value();
        //     if (preprocessing_success and one_step_result.realizability.value())
        //     {
        //         return true;
        //     }
        //     else if (preprocessing_success and !one_step_result.realizability.value())
        //     {
        //         return false;
        //     }
        //     else
        //     {
        //         std::cout << get_time_str()
        //                   << ": Preprocessing was not successful. Continuing with full DFA construction." << std::endl;
        //     }

        //     Syft::ExplicitStateDfa explicit_dfa = Syft::ExplicitStateDfa::dfa_of_formula(*formula);
        //     Syft::ExplicitStateDfaAdd explicit_dfa_add = Syft::ExplicitStateDfaAdd::from_dfa_mona(var_mgr,
        //                                                                                           explicit_dfa);

        //     Syft::SymbolicStateDfa symbolic_dfa = Syft::SymbolicStateDfa::from_explicit(
        //         std::move(explicit_dfa_add));
        //     var_mgr->partition_variables(partition.input_variables,
        //                                  partition.output_variables);

        //     Syft::Player starting_player = Syft::Player::Agent;
        //     Syft::Player protagonist_player = Syft::Player::Agent;
        //     Syft::LTLfSynthesizer synthesizer(symbolic_dfa, starting_player,
        //                                       protagonist_player, symbolic_dfa.final_states(),
        //                                       var_mgr->cudd_mgr()->bddOne());
        //     Syft::SynthesisResult result = synthesizer.run();

        //     return result.realizability;
        // }

        bool get_realizability_ltlfplus_from_input(const std::string &ltlfplus_formula, const std::vector<std::string> &input_variables,
                                                   const std::vector<std::string> &output_variables)
        {
            // LTLf+ driver
            std::shared_ptr<whitemech::lydia::parsers::ltlfplus::LTLfPlusDriver> driver =
                std::make_shared<whitemech::lydia::parsers::ltlfplus::LTLfPlusDriver>();

            // parse formula
            std::stringstream formula_stream(ltlfplus_formula);
            driver->parse(formula_stream);
            auto result = driver->get_result();

            // cast ast_ptr into ltlf_plus_ptr. Necessary since AbstractDriver is not template anymore
            auto ptr_ltlf_plus_formula =
                std::static_pointer_cast<const whitemech::lydia::LTLfPlusFormula>(result);

            // transform formula in PNF
            auto pnf = whitemech::lydia::get_pnf_result(*ptr_ltlf_plus_formula);
            Syft::LTLfPlus ltlf_plus_formula;
            ltlf_plus_formula.color_formula_ = pnf.color_formula_;
            ltlf_plus_formula.formula_to_color_ = pnf.subformula_to_color_;
            ltlf_plus_formula.formula_to_quantification_ = pnf.subformula_to_quantifier_;
            Syft::InputOutputPartition partition = Syft::InputOutputPartition::construct_from_input(input_variables,
                                                                                                    output_variables);
            Syft::Player starting_player = Syft::Player::Agent;

            Syft::LTLfPlusSynthesizer synthesizer(
                ltlf_plus_formula,
                partition,
                starting_player,
                Syft::Player::Agent);
            auto synthesis_result = synthesizer.run();
            return synthesis_result.realizability;
        }

        bool get_realizability_ltlfplusMP_from_input(const std::string &ltlfplus_formula, const std::vector<std::string> &input_variables,
                                                     const std::vector<std::string> &output_variables, int mp_solver)
        {
            // LTLf+ driver
            std::shared_ptr<whitemech::lydia::parsers::ltlfplus::LTLfPlusDriver> driver =
                std::make_shared<whitemech::lydia::parsers::ltlfplus::LTLfPlusDriver>();

            // parse formula
            std::stringstream formula_stream(ltlfplus_formula);
            driver->parse(formula_stream);
            auto result = driver->get_result();

            // cast ast_ptr into ltlf_plus_ptr. Necessary since AbstractDriver is not template anymore
            auto ptr_ltlf_plus_formula =
                std::static_pointer_cast<const whitemech::lydia::LTLfPlusFormula>(result);

            // transform formula in PNF
            auto pnf = whitemech::lydia::get_pnf_result(*ptr_ltlf_plus_formula);
            Syft::LTLfPlus ltlf_plus_formula;
            ltlf_plus_formula.color_formula_ = pnf.color_formula_;
            ltlf_plus_formula.formula_to_color_ = pnf.subformula_to_color_;
            ltlf_plus_formula.formula_to_quantification_ = pnf.subformula_to_quantifier_;
            Syft::InputOutputPartition partition = Syft::InputOutputPartition::construct_from_input(input_variables,
                                                                                                    output_variables);
            Syft::Player starting_player = Syft::Player::Agent;

            Syft::LTLfPlusSynthesizerMP synthesizer(
                ltlf_plus_formula,
                partition,
                starting_player,
                Syft::Player::Agent,
                mp_solver);
            auto synthesis_result = synthesizer.run();
            return synthesis_result.realizability;
        }

        bool get_realizability_ppltlfplus_from_input(const std::string &ppltlfplus_formula, const std::vector<std::string> &input_variables,
                                                     const std::vector<std::string> &output_variables)
        {
            // PPLTL+ driver
            std::shared_ptr<whitemech::lydia::parsers::ppltlplus::PPLTLPlusDriver> driver =
                std::make_shared<whitemech::lydia::parsers::ppltlplus::PPLTLPlusDriver>();

            // parse formula
            std::stringstream formula_stream(ppltlfplus_formula);
            driver->parse(formula_stream);
            auto result = driver->get_result();

            // cast ast_ptr into ppltl_plus_ptr
            auto ppltl_plus_ptr =
                std::static_pointer_cast<const whitemech::lydia::PPLTLPlusFormula>(result);

            // transform formula in PNF
            auto pnf = whitemech::lydia::get_pnf_result(*ppltl_plus_ptr);
            Syft::PPLTLPlus ppltl_plus_formula;
            ppltl_plus_formula.color_formula_ = pnf.color_formula_;
            ppltl_plus_formula.formula_to_color_ = pnf.subformula_to_color_;
            ppltl_plus_formula.formula_to_quantification_ = pnf.subformula_to_quantifier_;
            Syft::Player starting_player = Syft::Player::Agent;

            Syft::InputOutputPartition partition = Syft::InputOutputPartition::construct_from_input(input_variables,
                                                                                                    output_variables);

            Syft::PPLTLfPlusSynthesizer synthesizer(
                ppltl_plus_formula,
                partition,
                starting_player,
                Syft::Player::Agent);
            auto synthesis_result = synthesizer.run();
            return synthesis_result.realizability;
        }

        bool get_realizability_ppltlfplusMP_from_input(const std::string &ppltlplus_formula, const std::vector<std::string> &input_variables,
                                                     const std::vector<std::string> &output_variables, int mp_solver)
        {
            // PPLTL+ driver
            std::shared_ptr<whitemech::lydia::parsers::ppltlplus::PPLTLPlusDriver> driver =
                std::make_shared<whitemech::lydia::parsers::ppltlplus::PPLTLPlusDriver>();

            // parse formula
            std::stringstream formula_stream(ppltlplus_formula);
            driver->parse(formula_stream);
            auto result = driver->get_result();

            // cast ast_ptr into ppltl_plus_ptr
            auto ppltl_plus_ptr =
                std::static_pointer_cast<const whitemech::lydia::PPLTLPlusFormula>(result);

            // transform formula in PNF
            auto pnf = whitemech::lydia::get_pnf_result(*ppltl_plus_ptr);
            Syft::PPLTLPlus ppltl_plus_formula;
            ppltl_plus_formula.color_formula_ = pnf.color_formula_;
            ppltl_plus_formula.formula_to_color_ = pnf.subformula_to_color_;
            ppltl_plus_formula.formula_to_quantification_ = pnf.subformula_to_quantifier_;
            Syft::Player starting_player = Syft::Player::Agent;

            Syft::InputOutputPartition partition = Syft::InputOutputPartition::construct_from_input(input_variables,
                                                                                                    output_variables);

            Syft::PPLTLfPlusSynthesizerMP synthesizer(
                ppltl_plus_formula,
                partition,
                starting_player,
                Syft::Player::Agent,
                mp_solver);
            auto synthesis_result = synthesizer.run();
            return synthesis_result.realizability;
        }

    }
}