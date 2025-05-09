//
// Created by shuzhu on 28/04/25.
//

#include "catch2/catch_test_macros.hpp"
#include "catch2/generators/catch_generators_all.hpp"

#include <tuple>
#include "utils.hpp"
#include "game/InputOutputPartition.h"
#include "Synthesizer.h"
#include "synthesizer/LTLfPlusSynthesizer.h"

TEST_CASE("LTLf+ EL game test", "[test]")
{

    std::string boolean_formula = "(AE(F(e1 & X(ff))) -> AE(F(a1 & X(ff)))) & (EA(F(e2 & X(ff))) -> EA(F(a2 & X(ff)))) & (E(G(e3 -> F(a3))))";

    bool expected = true;
    INFO("tested\n");
    std::cout.flush();
    bool actual = Syft::Test::get_realizability_ltlfplus_from_input(boolean_formula, vars{"e1", "e2", "e3"}, vars{"a1", "a2", "a3"});
    REQUIRE(actual == expected);
}

TEST_CASE("LTLf+ EL game test1", "[test1]")
{

    std::string boolean_formula = "(AE(e1) -> AE(s1)) & (AE(e2) -> AE(s2)) & E(F(X(ff) & s3)) & (AE(e4) -> AE(s4)) & (AE(e5) -> AE(s5))";

    bool expected = true;
    INFO("tested\n");
    std::cout.flush();
    bool actual = Syft::Test::get_realizability_ltlfplus_from_input(boolean_formula, vars{"e1", "e2", "e3", "e4", "e5", "e6"}, vars{"s1", "s2", "s3", "s4", "s5", "s6"});
    REQUIRE(actual == expected);
}

TEST_CASE("LTLf+ EL game test2", "[test2]")
{

    std::string boolean_formula = "AE(a) && EA(b) && A(c) || E(d) || E(d1)";

    bool expected = true;
    INFO("tested\n");
    std::cout.flush();
    bool actual = Syft::Test::get_realizability_ltlfplus_from_input(boolean_formula, vars{"d", "d1"}, vars{"a", "b", "c"});
    REQUIRE(actual == expected);
}

TEST_CASE("LTLf+ EL game test3", "[test3]")
{

    std::string boolean_formula = "A(F((a & X[!](a | !a) & !(X[!](X[!](a | !a))))))";

    bool expected = false;
    INFO("tested\n");
    std::cout.flush();
    bool actual = Syft::Test::get_realizability_ltlfplus_from_input(boolean_formula, vars{}, vars{"a"});
    REQUIRE(actual == expected);
}

TEST_CASE("LTLf+ EL game test4", "[test4]")
{

    std::string boolean_formula = "(AE(a) & AE(b)) | EA(c) | EA(d) | A(e)";

    bool expected = true;
    INFO("tested\n");
    std::cout.flush();
    bool actual = Syft::Test::get_realizability_ltlfplus_from_input(boolean_formula, vars{"c", "d", "e"}, vars{"a", "b"});
    REQUIRE(actual == expected);
}

TEST_CASE("LTLf+ MP game test", "[test]")
{

    std::string boolean_formula = "(AE(F(e1 & X(ff))) -> AE(F(a1 & X(ff)))) & (EA(F(e2 & X(ff))) -> EA(F(a2 & X(ff)))) & (E(G(e3 -> F(a3))))";

    bool expected = true;
    INFO("tested\n");
    std::cout.flush();
    bool actual = Syft::Test::get_realizability_ltlfplusMP_from_input(boolean_formula, vars{"e1", "e2", "e3"}, vars{"a1", "a2", "a3"}, 1);
    REQUIRE(actual == expected);
}

TEST_CASE("LTLf+ MP game test1", "[test1]")
{

    std::string boolean_formula = "(AE(e1) -> AE(s1)) & (AE(e2) -> AE(s2)) & E(F(X(ff) & s3)) & (AE(e4) -> AE(s4)) & (AE(e5) -> AE(s5))";

    bool expected = true;
    INFO("tested\n");
    std::cout.flush();
    bool actual = Syft::Test::get_realizability_ltlfplusMP_from_input(boolean_formula, vars{"e1", "e2", "e3", "e4", "e5", "e6"}, vars{"s1", "s2", "s3", "s4", "s5", "s6"}, 1);
    REQUIRE(actual == expected);
}

TEST_CASE("LTLf+ MP game test2", "[test2]")
{

    std::string boolean_formula = "AE(a) && EA(b) && A(c) || E(d) || E(d1)";

    bool expected = true;
    INFO("tested\n");
    std::cout.flush();
    bool actual = Syft::Test::get_realizability_ltlfplusMP_from_input(boolean_formula, vars{"d", "d1"}, vars{"a", "b", "c"}, 1);
    REQUIRE(actual == expected);
}

TEST_CASE("LTLf+ MP game test3", "[test3]")
{

    std::string boolean_formula = "A(F((a & X[!](a | !a) & !(X[!](X[!](a | !a))))))";

    bool expected = false;
    INFO("tested\n");
    std::cout.flush();
    bool actual = Syft::Test::get_realizability_ltlfplusMP_from_input(boolean_formula, vars{"b"}, vars{"a"}, 1);
    REQUIRE(actual == expected);
}

TEST_CASE("LTLf+ MP game test4", "[test4]")
{

    std::string boolean_formula = "(AE(a) & AE(b)) | EA(c) | EA(d) | A(e)";

    bool expected = true;
    INFO("tested\n");
    std::cout.flush();
    bool actual = Syft::Test::get_realizability_ltlfplusMP_from_input(boolean_formula, vars{"c", "d", "e"}, vars{"a", "b"}, 1);
    REQUIRE(actual == expected);
}

TEST_CASE("LTLf+ MP Adv game test", "[test]")
{

    std::string boolean_formula = "(AE(F(e1 & X(ff))) -> AE(F(a1 & X(ff)))) & (EA(F(e2 & X(ff))) -> EA(F(a2 & X(ff)))) & (E(G(e3 -> F(a3))))";

    bool expected = true;
    INFO("tested\n");
    std::cout.flush();
    bool actual = Syft::Test::get_realizability_ltlfplusMP_from_input(boolean_formula, vars{"e1", "e2", "e3"}, vars{"a1", "a2", "a3"}, 2);
    REQUIRE(actual == expected);
}

TEST_CASE("LTLf+ MP Adv game test1", "[test1]")
{

    std::string boolean_formula = "(AE(e1) -> AE(s1)) & (AE(e2) -> AE(s2)) & E(F(X(ff) & s3)) & (AE(e4) -> AE(s4)) & (AE(e5) -> AE(s5))";

    bool expected = true;
    INFO("tested\n");
    std::cout.flush();
    bool actual = Syft::Test::get_realizability_ltlfplusMP_from_input(boolean_formula, vars{"e1", "e2", "e3", "e4", "e5", "e6"}, vars{"s1", "s2", "s3", "s4", "s5", "s6"}, 2);
    REQUIRE(actual == expected);
}

TEST_CASE("LTLf+ MP Adv game test2", "[test2]")
{

    std::string boolean_formula = "AE(a) && EA(b) && A(c) || E(d) || E(d1)";

    bool expected = true;
    INFO("tested\n");
    std::cout.flush();
    bool actual = Syft::Test::get_realizability_ltlfplusMP_from_input(boolean_formula, vars{"d", "d1"}, vars{"a", "b", "c"}, 2);
    REQUIRE(actual == expected);
}

TEST_CASE("LTLf+ MP Adv game test3", "[test3]")
{

    std::string boolean_formula = "A(F((a & X[!](a | !a) & !(X[!](X[!](a | !a))))))";

    bool expected = false;
    INFO("tested\n");
    std::cout.flush();
    bool actual = Syft::Test::get_realizability_ltlfplusMP_from_input(boolean_formula, vars{"b"}, vars{"a"}, 2);
    REQUIRE(actual == expected);
}

TEST_CASE("LTLf+ MP Adv game test4", "[test4]")
{

    std::string boolean_formula = "(AE(a) & AE(b)) | EA(c) | EA(d) | A(e)";

    bool expected = true;
    INFO("tested\n");
    std::cout.flush();
    bool actual = Syft::Test::get_realizability_ltlfplusMP_from_input(boolean_formula, vars{"c", "d", "e"}, vars{"a", "b"}, 2);
    REQUIRE(actual == expected);
}

