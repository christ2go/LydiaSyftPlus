#pragma once

#include <cstddef>
#include <vector>
#include "ELHelpers.hh"
#include "VarMgr.h"

struct ZielonkaNode {
    std::vector<ZielonkaNode*> children;
    ZielonkaNode *parent;
    size_t parent_order;
    size_t sibling_order;
    std::vector<bool> label;
    std::vector<CUDD::BDD> winningmoves;
    CUDD::BDD safenodes;
    CUDD::BDD targetnodes;
    size_t level;
    size_t order;
    bool winning;
    // std::vector<ZielonkaNode*> ancestors;
};

class ZielonkaTree {
private:
    // Private Variables
    ZielonkaNode *root;
    std::vector<std::string> phi; // Emerson-Lei condition in tokenized postfix format
    std::vector<CUDD::BDD> colorBDDs_;
    std::shared_ptr<Syft::VarMgr> var_mgr_;

    // Private methods
    void generate();
    void generate_parity();
    void generate_phi(const char*);
    void generate_phi_from_str(const std::string color_formula);
    bool evaluate_phi(std::vector<bool>);
    void displayZielonkaTree();
    void graphZielonkaTree();

public:
    ZielonkaTree(const std::string, const std::vector<CUDD::BDD>&, std::shared_ptr<Syft::VarMgr>);
    ~ZielonkaTree() {};

    ZielonkaNode* get_root();

    inline void print_label(ZielonkaNode *z) {
        for (size_t i = 0; i < z->label.size(); ++i){
            if (z->label[i]) {
                std::cout << static_cast<char>('a' + i) << ", ";
            } else {
                std::cout << "   ";
            }
        } //std::cout << '\n';
    }
};

