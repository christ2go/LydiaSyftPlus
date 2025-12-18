#ifndef OBLIGATION_FRAGMENT_DETECTOR_H
#define OBLIGATION_FRAGMENT_DETECTOR_H

#include <lydia/logic/ltlfplus/base.hpp>
#include <lydia/logic/ppltlplus/base.hpp>

namespace Syft {

/**
 * \brief Utility class for detecting obligation fragments in LTLf+ and PPLTL+ formulas.
 * 
 * The obligation fragment consists only of safety (forall) and guarantee (exists) 
 * quantifiers, excluding recurrence (forall-exists) and persistence (exists-forall).
 */
class ObligationFragmentDetector {
public:
    /**
     * \brief Check if an LTLf+ formula is in the obligation fragment.
     * 
     * \param formula The LTLf+ formula to check
     * \return true if the formula contains only safety and guarantee components
     */
    static bool isObligationFragment(const whitemech::lydia::ltlf_plus_ptr& formula);
    
    /**
     * \brief Check if a PPLTL+ formula is in the obligation fragment.
     * 
     * \param formula The PPLTL+ formula to check
     * \return true if the formula contains only safety and guarantee components
     */
    static bool isObligationFragment(const whitemech::lydia::ppltl_plus_ptr& formula);

private:
    /**
     * \brief Recursively check if an LTLf+ formula contains only obligation quantifiers.
     */
    static bool checkLTLfPlusObligation(const whitemech::lydia::ltlf_plus_ptr& formula);
    
    /**
     * \brief Recursively check if a PPLTL+ formula contains only obligation quantifiers.
     */
    static bool checkPPLTLPlusObligation(const whitemech::lydia::ppltl_plus_ptr& formula);
};

}

#endif