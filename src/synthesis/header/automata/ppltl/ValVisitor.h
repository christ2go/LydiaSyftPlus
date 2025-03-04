#ifndef VAL_VISITOR_H
#define VAL_VISITOR_H

#include"cuddObj.hh"
#include"VarMgr.h"
#include<lydia/lib/include/lydia/visitor.hpp>
#include<lydia/lib/include/lydia/utils/print.hpp>

namespace Syft {

    // typedef
    typedef whitemech::lydia::Visitor Visitor;
    typedef whitemech::lydia::PPLTLFormula PPLTLFormula;
    typedef whitemech::lydia::PPLTLTrue PPLTLTrue;
    typedef whitemech::lydia::PPLTLFalse PPLTLFalse;
    typedef whitemech::lydia::PPLTLAtom PPLTLAtom;
    typedef whitemech::lydia::PPLTLAnd PPLTLAnd;
    typedef whitemech::lydia::PPLTLOr PPLTLOr;
    typedef whitemech::lydia::PPLTLNot PPLTLNot;
    typedef whitemech::lydia::PPLTLYesterday PPLTLYesterday;
    typedef whitemech::lydia::PPLTLWeakYesterday PPLTLWeakYesterday; 
    typedef whitemech::lydia::PPLTLSince PPLTLSince; 
    typedef whitemech::lydia::PPLTLTriggered PPLTLTriggered;
    typedef whitemech::lydia::PPLTLOnce PPLTLOnce;
    typedef whitemech::lydia::PPLTLHistorically PPLTLHistorically;    
    typedef whitemech::lydia::StrPrinter StrPrinter;

    class ValVisitor : public Visitor {

        CUDD::BDD result;
        std::shared_ptr<VarMgr> mgr_;
        StrPrinter p;

        public:
            void visit(const PPLTLTrue& ) override;
            void visit(const PPLTLFalse& ) override;
            void visit(const PPLTLAtom& ) override;
            void visit(const PPLTLAnd& ) override;
            void visit(const PPLTLOr& ) override;
            void visit(const PPLTLNot& ) override;
            void visit(const PPLTLYesterday& ) override;
            void visit(const PPLTLWeakYesterday& ) override;
            void visit(const PPLTLSince& ) override;
            void visit(const PPLTLTriggered& ) override;
            void visit(const PPLTLOnce& ) override;
            void visit(const PPLTLHistorically& ) override;

        CUDD::BDD apply(const PPLTLFormula& b);

        ValVisitor(std::shared_ptr<VarMgr> mgr) : mgr_(mgr) {}

    };

    CUDD::BDD val(const PPLTLFormula& );
}
#endif // VAL_VISITOR_H