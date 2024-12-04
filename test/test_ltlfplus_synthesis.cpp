//
// Created by shuzhu on 04/12/24.
//

#include "catch2/catch_test_macros.hpp"
#include "catch2/generators/catch_generators_all.hpp"

#include <tuple>
#include "utils.hpp"
#include "game/InputOutputPartition.h"
#include "Synthesizer.h"
#include "synthesizer/LTLfPlusSynthesizer.h"





TEST_CASE("Game construction of email example", "[game construction]") {

    std::string boolean_formula = "p1 & !p2 & (p3 | p4)";
    Syft::LTLfPlus GFPhi_1, FGPhi_2, FPhi_3, GPhi_4;
    GFPhi_1.formula_ = "a";
    GFPhi_1.label_ = Syft::LTLfLabel::GF;
    FGPhi_2.formula_ = "b";
    FGPhi_2.label_ = Syft::LTLfLabel::FG;
    FPhi_3.formula_ = "c U b";
    FPhi_3.label_ = Syft::LTLfLabel::F;
    GPhi_4.formula_ = "d";
    GPhi_4.label_ = Syft::LTLfLabel::G;

std::map<char, Syft::LTLfPlus> spec = {
        { 'p1', GFPhi_1},
        { 'p2', FGPhi_2},
        { 'p3', FPhi_3},
        { 'p4', GPhi_4}
    };

bool expected = false;
   INFO("tested\n");
    std::cout.flush();
bool actual = Syft::Test::get_realizability_ltlfplus_from_input(spec, boolean_formula, vars{"d"}, vars{"a", "b", "c"});
REQUIRE(actual == expected);

}

