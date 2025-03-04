#include"ValVisitor.h"

namespace Syft {

    // val(true, σ, s) = true
    void ValVisitor::visit(const PPLTLTrue& x) {
        result = mgr_->cudd_mgr()->bddOne();
    }

    // val(false, σ, s) = false
    void ValVisitor::visit(const PPLTLFalse& x) {
        result = mgr_->cudd_mgr()->bddZero();
    }

    // val(x, σ, s) = x
    void ValVisitor::visit(const PPLTLAtom& x) {
        auto str = p.apply(x);
        result = mgr_->name_to_variable(str);
    }

    // val(f1 ∧ ... ∧ fn, σ, s) = ⋀_{i} val(fi, σ, s)
    void ValVisitor::visit(const PPLTLAnd& x) {
        result = mgr_->cudd_mgr()->bddOne();
        auto container = x.get_container();
        for (auto& a : container) result = result * apply(*a);
    }

    // val(f1 v ... v fn, σ, s) = V_{i} val(fi, σ, s)
    void ValVisitor::visit(const PPLTLOr& x) {
        result = mgr_->cudd_mgr()->bddZero();
        auto container = x.get_container();
        for (auto& a : container) result = result + apply(*a);
    }

    // val(!f, σ, s) = !val(f, σ, s)
    void ValVisitor::visit(const PPLTLNot& x) {
        result = !(apply(*x.get_arg()));
    }

    // val(Yf, σ, s) = Yf
    void ValVisitor::visit(const PPLTLYesterday& x) {
        auto str = p.apply(x);
        result = mgr_->name_to_variable(str);
    }

    // val(WYf, σ, s) = WYf
    void ValVisitor::visit(const PPLTLWeakYesterday& x) {
        auto str = p.apply(x);
        result = mgr_->name_to_variable(str);
    }

    // val(f1 S f2, σ, s) = val(f2 v (f1 ∧ Y(f1 S f2)), σ, s)
    void ValVisitor::visit(const PPLTLSince& x) {
        auto arg1 = x.get_args()[0]; // f1
        auto arg2 = x.get_args()[1]; // f2
        auto s = x.ctx().makePPLTLSince(arg1, arg2);
        auto ys = x.ctx().makePPLTLYesterday(s); // Y(f1 S f2)
        auto r = x.ctx().makePPLTLAnd({arg1, ys}); // (f1 ∧ Y(f1 S f2))
        auto l = x.ctx().makePPLTLOr({arg2, r}); // f2 v (f1 ∧ Y(f1 S f2))
        result = apply(*l);
    }

    // val(Of, σ, s) = val(f v Y(O(f)), σ, s)
    void ValVisitor::visit(const PPLTLOnce& x) {
        auto arg = x.get_arg(); // f
        auto o = x.ctx().makePPLTLOnce(arg); // O(f)
        auto yo = x.ctx().makePPLTLYesterday(o); // Y(O(f))
        auto r = x.ctx().makePPLTLOr({arg, yo}); // f v Y(O(f))
        result = apply(*r);
    }

    // val(Hf, σ, s) = val(f ∧ WY(H(f), σ, s)
    void ValVisitor::visit(const PPLTLHistorically& x) {
        auto arg = x.get_arg(); // f
        auto h = x.ctx().makePPLTLHistorically(arg); // H(f)
        auto wyh = x.ctx().makePPLTLWeakYesterday(h); // WY(H(f))
        auto r = x.ctx().makePPLTLAnd({arg, h});
        result = apply(*r);
    }

    // val(f1 T f2, σ, s) = val(f2 ∧ (f1 v WY(f1 T f2), σ, s)
    void ValVisitor::visit(const PPLTLTriggered& x) {
        auto arg1 = x.get_args()[0]; // f1
        auto arg2 = x.get_args()[1]; // f2
        auto t = x.ctx().makePPLTLTriggered(arg1, arg2); // f1 T f2
        auto wyt = x.ctx().makePPLTLWeakYesterday(t); // WY(f1 T f2)
        auto r = x.ctx().makePPLTLOr({arg1, wyt}); // (f1 v WY(f1 T f2))
        auto l = x.ctx().makePPLTLAnd({arg2, r}); // f1 ∧ (f1 v Y(f1 S f2))
        result = apply(*l); 
    }

    CUDD::BDD ValVisitor::apply(const PPLTLFormula& x) {
        x.accept(*this);
        return result;
    }

    CUDD::BDD val(const PPLTLFormula& x, std::shared_ptr<VarMgr> mgr) {
        ValVisitor v(mgr);
        return v.apply(x);
    }
}