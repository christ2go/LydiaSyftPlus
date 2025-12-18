#include "ObligationFragmentDetector.h"
#include <lydia/logic/ltlfplus/base.hpp>

namespace Syft {

using whitemech::lydia::Basic;
using whitemech::lydia::is_a;
using whitemech::lydia::ltlf_ptr;
using whitemech::lydia::ltlf_plus_ptr;

using whitemech::lydia::LTLfPlusTrue;
using whitemech::lydia::LTLfPlusFalse;
using whitemech::lydia::LTLfPlusAnd;
using whitemech::lydia::LTLfPlusOr;
using whitemech::lydia::LTLfPlusNot;
using whitemech::lydia::LTLfPlusExists;
using whitemech::lydia::LTLfPlusForall;
using whitemech::lydia::LTLfPlusExistsForall;
using whitemech::lydia::LTLfPlusForallExists;

// In this codebase, LTLfPlus quantifiers take an `ltlf_ptr` argument,
// which is “plain LTLf” (no LTLfPlus quantifiers), so it's quantifier-free.
// If you later add FO quantifiers inside ltlf, implement that check here.
static bool is_quantifier_free_ltlf(const ltlf_ptr&) {
    return true;
}

bool ObligationFragmentDetector::isObligationFragment(const ltlf_plus_ptr& formula) {
    return checkLTLfPlusObligation(formula);
}

bool ObligationFragmentDetector::checkLTLfPlusObligation(const ltlf_plus_ptr& formula) {
    if (!formula) return false;

    const Basic& b = *formula;

    // Optional: allow boolean constants
    if (is_a<LTLfPlusTrue>(b) || is_a<LTLfPlusFalse>(b)) return true;

    // Allowed “atoms”: Exists(phi), Forall(phi) with quantifier-free phi
    if (is_a<LTLfPlusExists>(b)) {
        const auto& e = dynamic_cast<const LTLfPlusExists&>(b);
        return is_quantifier_free_ltlf(e.get_arg());
    }
    if (is_a<LTLfPlusForall>(b)) {
        const auto& a = dynamic_cast<const LTLfPlusForall&>(b);
        return is_quantifier_free_ltlf(a.get_arg());
    }

    // Allowed boolean structure
    if (is_a<LTLfPlusNot>(b)) {
        const auto& n = dynamic_cast<const LTLfPlusNot&>(b);
        return checkLTLfPlusObligation(n.get_arg());
    }
    if (is_a<LTLfPlusAnd>(b)) {
        const auto& cont = dynamic_cast<const LTLfPlusAnd&>(b).get_container();
        for (const auto& sub : cont)
            if (!checkLTLfPlusObligation(sub)) return false;
        return true;
    }
    if (is_a<LTLfPlusOr>(b)) {
        const auto& cont = dynamic_cast<const LTLfPlusOr&>(b).get_container();
        for (const auto& sub : cont)
            if (!checkLTLfPlusObligation(sub)) return false;
        return true;
    }

    // Explicitly reject the mixed quantifier blocks (not in your fragment)
    if (is_a<LTLfPlusExistsForall>(b) || is_a<LTLfPlusForallExists>(b)) return false;

    // Everything else: nope
    return false;
}

} // namespace Syft
