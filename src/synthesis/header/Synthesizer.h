#ifndef SYNTHESIZER_H
#define SYNTHESIZER_H

#include <memory>

#include "game/Transducer.h"
#include "game/ZielonkaTree.hh"
#include "lydia/logic/pnf.hpp"
#include "lydia/logic/pp_pnf.hpp"
#include <optional>


namespace Syft {

    static const std::string REALIZABLE_STR = "REALIZABLE";
    static const std::string UNREALIZABLE_STR = "UNREALIZABLE";

    struct SynthesisResult {
        bool realizability;
        CUDD::BDD winning_states;
        CUDD::BDD winning_moves;
        std::unique_ptr<Transducer> transducer;
        CUDD::BDD safe_states;
    };

    struct ELWinningMove {
        CUDD::BDD gameNode;
        ZielonkaNode* t;
        CUDD::BDD Y;
        ZielonkaNode* u;
    };

    typedef std::vector<ELWinningMove> EL_output_function;
    struct ELSynthesisResult {
        bool realizability;
        CUDD::BDD winning_states;
        EL_output_function output_function;
        ZielonkaTree* z_tree = nullptr;
    };


    struct MPWinningMove {
        CUDD::BDD gameNode;
        int currDagNodeId;
        ZielonkaNode* t;
        CUDD::BDD Y;
        int newDagNodeId;
        ZielonkaNode* u;
    };
    typedef std::vector<MPWinningMove> MP_output_function;
    struct MPSynthesisResult {
        bool realizability;
        CUDD::BDD winning_states;
        MP_output_function output_function;
    };

    struct MaxSetSynthesisResult {
        bool realizability;
        CUDD::BDD deferring_strategy;
        CUDD::BDD nondeferring_strategy;
    };

    struct OneStepSynthesisResult {
        std::optional<bool> realizability = std::nullopt;
        CUDD::BDD winning_move;
    };

    struct LTLfPlus {
        std::string color_formula_;
        std::unordered_map<whitemech::lydia::ltlf_plus_ptr, std::string> formula_to_color_;
        std::unordered_map<whitemech::lydia::ltlf_plus_ptr, whitemech::lydia::PrefixQuantifier> formula_to_quantification_;
    };

    struct PPLTLPlus {
        std::string color_formula_;
        std::unordered_map<whitemech::lydia::ppltl_plus_ptr, std::string> formula_to_color_;
        std::unordered_map<whitemech::lydia::ppltl_plus_ptr, whitemech::lydia::PrefixQuantifier> formula_to_quantification_;
    };

/**
 * \brief Abstract class for synthesizers.
 *
 * Can be inherited to implement synthesizers for different specification types.
 */
    template<class Spec>
    class Synthesizer {
    protected:
        /**
         * \brief The game arena.
         */
        Spec spec_;

    public:

        Synthesizer(Spec spec)
                : spec_(std::move(spec)) {}


        virtual ~Synthesizer() {}

        /**
         * \brief Solves the synthesis problem of the specification.
         *
         * \return The result consists of
         * realizability
         * a set of agent winning states
         * a transducer representing a winning strategy for the specification or nullptr if the specification is unrealizable.
         */
        virtual SynthesisResult run() const = 0;
    };

}

#endif // SYNTHESIZER_H