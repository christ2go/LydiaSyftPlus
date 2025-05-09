//
// Created by shuzhu on 28/04/25.
//

#include "catch2/catch_test_macros.hpp"
#include "catch2/generators/catch_generators_all.hpp"

#include <tuple>
#include "utils.hpp"
#include "game/InputOutputPartition.h"
#include "Synthesizer.h"
#include "synthesizer/PPLTLfPlusSynthesizer.h"


TEST_CASE("PPLTLf+ EL game test", "[test]")
{

    std::string boolean_formula = "A(Y(a))";

    bool expected = false;
    INFO("tested\n");
    std::cout.flush();
    bool actual = Syft::Test::get_realizability_ppltlfplus_from_input(boolean_formula, vars{}, vars{"a"});
    REQUIRE(actual == expected);
}

TEST_CASE("PPLTLf+ MP game test", "[test]")
{

    std::string boolean_formula = "A(Y(a))";

    bool expected = false;
    INFO("tested\n");
    std::cout.flush();
    bool actual = Syft::Test::get_realizability_ppltlfplusMP_from_input(boolean_formula, vars{}, vars{"a"}, 1);
    REQUIRE(actual == expected);
}

TEST_CASE("PPLTLf+ MP Adv game test", "[test]")
{

    std::string boolean_formula = "A(Y(a))";

    bool expected = false;
    INFO("tested\n");
    std::cout.flush();
    bool actual = Syft::Test::get_realizability_ppltlfplusMP_from_input(boolean_formula, vars{}, vars{"a"}, 2);
    REQUIRE(actual == expected);
}


