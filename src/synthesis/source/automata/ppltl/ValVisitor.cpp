#include"automata/ppltl/ValVisitor.h"

namespace Syft {

    // val(true, σ, s) = true
    void ValVisitor::visit(const PPLTLTrue& x) {
        CUDD::BDD r = mgr_->cudd_mgr()->bddOne();
        result = std::move(r);
    }

    // val(false, σ, s) = false
    void ValVisitor::visit(const PPLTLFalse& x) {
        CUDD::BDD r = mgr_->cudd_mgr()->bddZero();
        result = std::move(r);
    }

    // val(x, σ, s) = x
    void ValVisitor::visit(const PPLTLAtom& x) {
        auto str = p.apply(x);
        CUDD::BDD r = mgr_->name_to_variable(str);
        result = std::move(r);
    }

    // val(f1 ∧ ... ∧ fn, σ, s) = ⋀_{i} val(fi, σ, s)
    void ValVisitor::visit(const PPLTLAnd& x) {
        CUDD::BDD r = mgr_->cudd_mgr()->bddOne();
        auto container = x.get_container();
        for (auto& a : container) r = r * apply(*a);
        result = std::move(r);
    }

    // val(f1 v ... v fn, σ, s) = V_{i} val(fi, σ, s)
    void ValVisitor::visit(const PPLTLOr& x) {
        CUDD::BDD r = mgr_->cudd_mgr()->bddZero();
        auto container = x.get_container();
        for (auto& a : container) r = r + apply(*a);
        result = std::move(r);
    }

    // val(!f, σ, s) = !val(f, σ, s)
    void ValVisitor::visit(const PPLTLNot& x) {
        CUDD::BDD r = !(apply(*x.get_arg()));
        result = std::move(r);
    }

    // val(Yf, σ, s) = Yf
    void ValVisitor::visit(const PPLTLYesterday& x) {
        auto str = p.apply(x);
        CUDD::BDD r = mgr_->name_to_variable(str);
        result = std::move(r);
    }

    // val(WYf, σ, s) = WYf
    void ValVisitor::visit(const PPLTLWeakYesterday& x) {
        auto str = p.apply(x);
        CUDD::BDD r = mgr_->name_to_variable(str);
        result = std::move(r);
    }

    // val(f1 S f2, σ, s) = val(f2 v (f1 ∧ Y(f1 S f2)), σ, s)
    void ValVisitor::visit(const PPLTLSince& x) {
        auto arg1 = x.get_args()[0]; // f1
        auto arg2 = x.get_args()[1]; // f2
        auto b1 = apply(*arg1); // val(f1)
        auto b2 = apply(*arg2); // val(f2)
        auto y = x.ctx().makePPLTLYesterday(x.ctx().makePPLTLSince(arg1, arg2)); // Y(f1 S f2)
        auto yb = apply(*y); // val(Y(f1 S f2))
        auto r = b2 + (b1 * yb); // val(f2) + (val(f1) * val(Y(f1 S f2)))
        result = std::move(r);
    }

    // val(Of, σ, s) = val(f v Y(O(f)), σ, s)
    void ValVisitor::visit(const PPLTLOnce& x) {
        auto arg = x.get_arg(); // f
        auto b = apply(*arg); // val(f)
        auto y = x.ctx().makePPLTLYesterday(x.ctx().makePPLTLOnce(arg)); // Y(O(f))
        auto yb = apply(*y); // val(Y(O(f)))
        auto r = b + yb; // val(f) + val(Y(O(f)))
        result = std::move(r);
    }

    // val(Hf, σ, s) = val(f ∧ WY(H(f), σ, s)
    void ValVisitor::visit(const PPLTLHistorically& x) {
        auto arg = x.get_arg(); // f
        auto b = apply(*arg); // val(f)
        auto y = x.ctx().makePPLTLWeakYesterday(x.ctx().makePPLTLHistorically(arg)); // WY(H(f))
        auto yb = apply(*y); // val(WY(H(f)))
        auto r = b * yb; // val(f) * val(WY(H(f)))
        result = std::move(r);
    }

    // val(f1 T f2, σ, s) = val(f1 ∧ (f2 v WY(f1 T f2), σ, s)
    void ValVisitor::visit(const PPLTLTriggered& x) {
        auto arg1 = x.get_args()[0]; // f1
        auto arg2 = x.get_args()[1]; // f2
        auto b1 = apply(*arg1); // val(f1)
        auto b2 = apply(*arg2); // val(f2)
        auto y = x.ctx().makePPLTLWeakYesterday(x.ctx().makePPLTLTriggered(arg1, arg2)); // WY(f1 T f2)
        auto yb = apply(*y); // val(WY(f1 T f2))
        auto r = b1 * (b2 + yb); // val(f1) * (val(f2) + val(WY(f1 T f2)))
        result = std::move(r);
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