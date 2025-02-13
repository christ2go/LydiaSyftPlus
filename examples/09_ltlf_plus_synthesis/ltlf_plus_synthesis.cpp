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
    // GFPhi_1 & FGPhi_2 & (FPhi_3 | GPhi_4)
    std::string color_formula = "(0 & !1) & (2 | 3)";
    Syft::LTLfPlus GFPhi_0, FGPhi_1, GPhi_2, FPhi_3;

    GFPhi_0.label_ = Syft::LTLfLabel::GF;
    GFPhi_0.formula_ = "a";

    FGPhi_1.label_ = Syft::LTLfLabel::FG;
    FGPhi_1.formula_ = "b";

    GPhi_2.label_ = Syft::LTLfLabel::G;
    GPhi_2.formula_ = "c";

    FPhi_3.label_ = Syft::LTLfLabel::F;
    FPhi_3.formula_ = "d";

    std::map<char, Syft::LTLfPlus> spec = {
            { '0', GFPhi_0},
            { '1', FGPhi_1},
            { '2', GPhi_2},
            { '3', FPhi_3}
    };
    std::vector<std::string> input_variables{"d"};
    std::vector<std::string> output_variables{"a", "b", "c"};

    Syft::InputOutputPartition partition = Syft::InputOutputPartition::construct_from_input(input_variables,
                                                                                            output_variables);
    Syft::Player starting_player = Syft::Player::Agent;
    Syft::Player protagonist_player = Syft::Player::Agent;
    Syft::LTLfPlusSynthesizer synthesizer(spec, color_formula, partition, starting_player, protagonist_player);
    Syft::SynthesisResult result = synthesizer.run();
    if (result.realizability == true) {
        std::cout<< "Realizable" <<std::endl;
    } else {
        std::cout<< "Unrealizable" <<std::endl;
    }

}

