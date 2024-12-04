#include "game/ZielonkaTree.hh"
#include "game/ELHelpers.hh"
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <queue>


bool cmp_descending_count_true(const std::vector<bool>& a, const std::vector<bool>& b) {
    return std::count(a.begin(), a.end(), true) > std::count(b.begin(), b.end(), true);
}

// Private
size_t leaves = 0;
size_t total_nodes = 0;

void ZielonkaTree::generate() {
    //std::cout << "generating... \n";
    std::queue<ZielonkaNode*> q;
    q.push(root);
    std::vector<std::vector<bool>> ps = ELHelpers::powerset(root->label.size());
    std::sort(ps.begin(), ps.end(), cmp_descending_count_true);
    size_t order = root->order + 1;
    std::vector<std::vector<bool>> seen_from_parent{};
    while (!q.empty()) {
        seen_from_parent.clear();
        ZielonkaNode* current = q.front();
        q.pop();
        total_nodes++;
        for (size_t i = 0; i < ps.size(); ++i) {
            std::vector<bool> color_set = ps[i];
            if (!ELHelpers::proper_subset(color_set, current->label))
                continue;
            bool seen = false;
            for (const auto& s : seen_from_parent) {
                if (ELHelpers::proper_subset(color_set, s)) {
                    seen = true;
                    break;
                }
            }
            if (seen) continue;
            if (evaluate_phi(color_set) != current->winning) {
                ZielonkaNode *child_zn = new ZielonkaNode {
                    .children = {},
//                    .parentdiff = ELHelpers::label_difference(current->label, color_set),
                    .parent = current,
                    .parent_order = current->order,
                    .label = color_set,
                    .avoidnodes = current->avoidnodes & ELHelpers::negIntersectionOf(ELHelpers::label_difference(current->label, color_set), colorBDDs_),
		            .targetnodes = current->avoidnodes & ELHelpers::unionOf(ELHelpers::label_difference(current->label, color_set), colorBDDs_),
                    .level = current->level + 1,
                    .order = order++,
                    .winning = !(current->winning)
                };
                seen_from_parent.push_back(color_set);
                current->children.push_back(child_zn);
                q.push(child_zn);
            }
        }
        if (current->children.empty()) leaves++;
    }
    //std::cout << "done generating...\n";
}
void ZielonkaTree::generate_parity() {
    // firstly evaluate root, then remove the last color from the current colorset
    //std::cout << "generating... \n";
    size_t order = root->order + 1;
    std::vector<bool> colors;
    for (const bool& b : root->label)
        colors.push_back(b);
    ZielonkaNode* current = root;
    for (int i = colors.size()-1; i >= 0; --i) {
        colors[i] = false;
        ZielonkaNode *child_zn = new ZielonkaNode {
            .children = {},
//            .parentdiff = ELHelpers::label_difference(current->label, colors),
            .parent = current,
            .parent_order = current->order,
            .label = colors,
            .avoidnodes = current->avoidnodes & ELHelpers::negIntersectionOf(ELHelpers::label_difference(current->label, colors), colorBDDs_),
	        .targetnodes = current->avoidnodes & ELHelpers::unionOf(ELHelpers::label_difference(current->label, colors), colorBDDs_),
            .level = current->level + 1,
            .order = order++,
            .winning = !(current->winning)
        };
        current->children.push_back(child_zn);
        current = child_zn;
    }
}

void ZielonkaTree::generate_phi(const char* conditionFile){
    std::ifstream ifs(conditionFile);
    std::string condition((std::istreambuf_iterator<char>(ifs)),
                          (std::istreambuf_iterator<char>()));

    if (condition.empty()){
        std::cout << "Condition empty, exiting..." << std::endl;
        exit(1);
    }

    phi = ELHelpers::infix2postfix(ELHelpers::tokenize(condition));
}

void ZielonkaTree::generate_phi_from_str(const std::string color_formula){
//    std::ifstream ifs(conditionFile);
//    std::string condition((std::istreambuf_iterator<char>(ifs)),
//                          (std::istreambuf_iterator<char>()));

    if (color_formula.empty()){
        std::cout << "Condition empty, exiting..." << std::endl;
        exit(1);
    }

    phi = ELHelpers::infix2postfix(ELHelpers::tokenize(color_formula));
}

std::string label_to_string(std::vector<bool> label) {
    std::string s;
    for (size_t i = 0; i < label.size(); ++i) {
        if (label[i])
            s += static_cast<char>('a' + i);
    }
    if (s.empty()) return "∅";
    return s;
}

bool ZielonkaTree::evaluate_phi(std::vector<bool> colors) {
    return ELHelpers::eval_postfix(phi, colors);
}

void ZielonkaTree::displayZielonkaTree() {
    // BFS
    std::cout << "displaying...\n";
    std::queue<ZielonkaNode*> q;
    q.push(root);
    while (!q.empty()) {
        ZielonkaNode* current = q.front();
        print_label(current);
        std::cout << "from: "
                  << current->parent_order
                  << ", order: "
                  << current->order << '\n';
//        for (size_t i = 0; i < current->children.size(); ++i) {
//            std::cout << label_to_string(current->child_differences[i]) << '\n';
//        }
        std::cout << '\n';
        q.pop();
        for (ZielonkaNode *z : current->children) {
            q.push(z);
        }
    }
}




void printNTree(ZielonkaNode* x, std::vector<bool> flag, int depth = 0, bool isLast = false) {
    // Taken from https://www.geeksforgeeks.org/print-n-ary-tree-graphically/
    // Condition when node is None
    if (x == NULL)
        return;

    // Loop to print the depths of the
    // current node
    for (int i = 1; i < depth; ++i) {
        // Condition when the depth
        // is exploring
        if (flag[i] == true) {
            std::cout << "│ "
                << " "
                << " "
                << " ";
        }
        // Otherwise print 
        // the blank spaces
        else {
            std::cout << " "
                << " "
                << " "
                << " ";
        }
    }
    // Condition when the current
    // node is the root node
    if (depth == 0)
        std::cout << label_to_string(x->label) << " " << (x->winning? 'W' : 'L') << '\n';

    // Condition when the node is 
    // the last node of 
    // the exploring depth
    else if (isLast) {
        std::cout << "└── " << label_to_string(x->label) << " " << (x->winning? 'W' : 'L') << '\n';
         
        // No more childrens turn it 
        // to the non-exploring depth
        flag[depth] = false;
    }
    else {
        std::cout << "├── " << label_to_string(x->label) << " " << (x->winning? 'W' : 'L') << '\n';
    }
 
    size_t it = 0;
    for (auto i = x->children.begin();
    i != x->children.end(); ++i, ++it)
 
        // Recursive call for the
        // children nodes
        printNTree(*i, flag, depth + 1, 
            it == (x->children.size()) - 1);
    flag[depth] = true;
}

void ZielonkaTree::graphZielonkaTree() {
    printNTree(root, std::vector<bool>(85, true));
}

// Public
ZielonkaTree::ZielonkaTree(const std::string color_formula, const std::vector<CUDD::BDD> &colorBDDs, std::shared_ptr<Syft::VarMgr> var_mgr) :  colorBDDs_(colorBDDs), var_mgr_(var_mgr){
    generate_phi_from_str(color_formula);
    std::vector<bool> label( colorBDDs.size(), true);
    root = new ZielonkaNode {
        .children  = {},
        .parent = nullptr,
        .parent_order = 0,
        .label = label,
	.avoidnodes = var_mgr_->cudd_mgr()->bddOne(),
	.targetnodes = var_mgr_->cudd_mgr()->bddOne(),
        .level = 1,
        .order = 1,
        .winning = evaluate_phi(label)
    };
//    this.colorBDDs = colorBDDs;
    generate();
    //generate_parity();
    //graphZielonkaTree();
    std::cout << "leaves: "<< leaves << '\n';
    std::cout << "nodes: " << total_nodes  << '\n';
    //displayZielonkaTree();
    graphZielonkaTree();
}

ZielonkaNode* ZielonkaTree::get_root() { return root; }

