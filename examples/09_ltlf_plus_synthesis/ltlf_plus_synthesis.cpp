//
// Created by shuzhu on 04/12/24.
//
#include <filesystem>
#include <memory>
#include <string>

#include <lydia/parser/ltlf/driver.hpp>

#include "automata/SymbolicStateDfa.h"
#include "synthesizer/LTLfPlusSynthesizer.h"
#include "Player.h"
#include "VarMgr.h"
#include "Utils.h"
#include "Preprocessing.h"


int main(int argc, char ** argv) {

    std::string color_formula = "1 & !2 & (3 | 4)";
    Syft::LTLfPlus GFPhi_1, FGPhi_2, FPhi_3, GPhi_4;

    GFPhi_1.label_ = Syft::LTLfLabel::GF;
    GFPhi_1.formula_ = "a";

    FGPhi_2.label_ = Syft::LTLfLabel::FG;
    FGPhi_2.formula_ = "b";

    FPhi_3.label_ = Syft::LTLfLabel::G;
    FPhi_3.formula_ = "Fc";

    GPhi_4.label_ = Syft::LTLfLabel::F;
    GPhi_4.formula_ = "Gd";

    std::map<char, Syft::LTLfPlus> spec = {
            { '1', GFPhi_1},
            { '2', FGPhi_2},
            { '3', FPhi_3},
            { '4', GPhi_4}
    };
    std::vector<std::string> input_variables{"d"};
    std::vector<std::string> output_variables{"a", "b", "c"};

    Syft::InputOutputPartition partition = Syft::InputOutputPartition::construct_from_input(input_variables,
                                                                                            output_variables);
    Syft::Player starting_player = Syft::Player::Agent;
    Syft::Player protagonist_player = Syft::Player::Agent;
    Syft::LTLfPlusSynthesizer synthesizer(spec, color_formula, partition, starting_player, protagonist_player);
    Syft::SynthesisResult result = synthesizer.run();

}

