#pragma once

#include <algorithm>
#include <cassert>
#include <iostream>
#include <cmath>
#include <iterator>
#include <vector>
#include <regex>
#include "game/ZielonkaTree.hh"
#include "VarMgr.h"

namespace ELHelpers {

    inline bool proper_subset(const std::vector<bool>& l1, const std::vector<bool>& l2) {
        // Check if l1 subset of l2
        // forall i in l1: if l1 then also l2
        // but there must exist at least one index in l2 which is not in l1
        bool flag = false;
        for (size_t i = 0; i < l1.size(); ++i) {
            if (l2[i] && !l1[i])
                flag = true;
            if (l1[i] && !l2[i])
                return false;
        }
        return flag;
    }



    inline std::vector<bool> label_difference(const std::vector<bool>& t, const std::vector<bool>& s) {
        // compute difference t-s, where s,t are vectors of colors
        size_t size = t.size();
        std::vector<bool> difference(size, false);
        for (size_t i = 0; i < size; ++i)
            difference[i] = t[i] && !s[i];
        return difference;
    }


    inline CUDD::BDD unionOf(const std::vector<bool>& col, const std::vector<CUDD::BDD>& colorBDDs) {
        // return union of all color bdds indicated by col
//        CUDD::BDD result = mgr_.bddZero();
        CUDD::BDD result = colorBDDs[0];
        for (size_t i = 1; i < col.size(); ++i){
            if (col[i])
            result |= colorBDDs[i]; // result = result | NodesThatSeeColor(i)
        }
        return result;
    }

    inline CUDD::BDD negIntersectionOf(const std::vector<bool>& col, const std::vector<CUDD::BDD>& colorBDDs) {
        // return intersection of all negated color bdds indicated by col
        CUDD::BDD result = colorBDDs[0];
        for (size_t i = 1; i < col.size(); ++i){
            if (col[i])
            result &= (colorBDDs[colorBDDs.size()/2 + i]); // result = result & NodesThatDoNotSeeColor(i)
        }
        return result;
    }

    // Standard algorithm for powerset generation: https://www.geeksforgeeks.org/power-set/
    inline std::vector<std::vector<bool>> powerset(size_t colors) {
        // remake this so that it works with new evaluation function (std::vector<bool>)
        size_t ps_size = pow(2, colors);
        std::vector<std::vector<bool>> ps;
        for (size_t count = 0; count < ps_size; ++count) {
            std::vector<bool> tmp(colors, 0);
            for (size_t bit = 0; bit < colors; ++bit) {
                if (count & (1 << bit))
                    tmp[bit] = true;
            }
            ps.push_back(tmp);
        }
        return ps;
    }

    inline std::vector<size_t> preprocess_to_UBDD(const std::vector<bool>& label) {
        std::vector<size_t> preprocessed;
        for (size_t i = 0; i < label.size(); ++i){
            if (label[i]) preprocessed.push_back(i);
        } return preprocessed;
    }


    // EVALUATION OF BOOLEAN FORMULAS
    //
    // INPUT ALPHABET
    // a | !a | (a) | a&a | a|a
    // (a = Inf(a), !a = Fin(a))
    //
    // INPUT EXAMPLE
    // 0 & !1 | (1 | 2)
    // Numbers represent variable indices, 0 -> variable 0.

    // Debug function for printing tokens in vector<string>
    inline void print_tokens(std::vector<std::string> input){
        std::cout << "[";
        for (std::string i : input){
            std::cout << i << ", ";
        }
        std::cout << "]" << std::endl;
    }

    // Tokenize input string to following alphabet:
    // op (!,&,|) | a | ( | )
    inline std::vector<std::string> tokenize(std::string input){
        std::vector<std::string> result;
        std::regex inf_re("Inf");
        std::regex fin_re("Fin");
        input = std::regex_replace(input, inf_re, "");
        input = std::regex_replace(input, fin_re, "!");

        for (size_t i=0; i<input.size(); i++){
            std::string s = "";
            char inp = input[i];
            if (inp == ' ')
                continue;
            else if (isdigit(inp))
            {
                s += inp;
                i++;
                for (; i<input.size(); i++){
                    inp = input[i];
                    if (!isdigit(inp))
                        break;
                    s += inp;
                }
                i--;
            }
            else
                s += input[i];

            result.push_back(s);
        }
        return result;
    }

    // Helper for infix2postfix
    inline bool isNumber(std::string s){
        for (char c : s){
            if (!isdigit(c))
                return false;
        }
        return true;
    }

    // Helper for infix2postfix
    inline bool isOperator(std::string s){
        if (s == "&" || s == "|" || s == "!")
            return true;
        return false;
    }

    // Takes tokenized input string (in infix) and translates to postfix
    inline std::vector<std::string> infix2postfix(std::vector<std::string> tokens){
        std::vector<std::string> opStack;
        std::vector<std::string> outputStack;

        for (std::string s : tokens){
            if (isOperator(s)){
                if (opStack.empty())
                    opStack.push_back(s);
                else{
                    if (opStack.back() == "(" || s == "!"){
                        opStack.push_back(s);
                        continue;
                    }
                    std::string tmp = opStack.back();
                    opStack.pop_back();
                    outputStack.push_back(tmp);
                    opStack.push_back(s);
                }
            }
            else if (isNumber(s)){
                outputStack.push_back(s);
                if (!opStack.empty()){
                    if (opStack.back() == "!"){
                    outputStack.push_back(opStack.back());
                    opStack.pop_back();
                }
                }
            }
            else if (s == "("){
                opStack.push_back(s);
            }
            else{ // s == ")"
                while (true){
                    if (opStack.empty())
                        break;
                    if (opStack.back() == "("){
                        opStack.pop_back();
                        break;
                    }
                    std::string tmp = opStack.back();
                    outputStack.push_back(tmp);
                    opStack.pop_back();                
                }
            }
        }

        for (std::string s : opStack){
            std::string tmp = opStack.back();
            outputStack.push_back(tmp);
            opStack.pop_back();
        }

        return outputStack;
    }

    inline bool eval_postfix (std::vector<std::string> postfix, std::vector<bool> colors){
        std::reverse(postfix.begin(), postfix.end());
        std::vector<bool> resStack;

        while (!postfix.empty()){
            std::string s = postfix.back();
            postfix.pop_back();

            if (isNumber(s)){
                int tmp = stoi(s);
                //std::cout << "var " << tmp << std::endl;
                resStack.push_back(colors[tmp]);
            }
            else{
                if (s == "!"){
                    bool tmp = resStack.back();
                    resStack.pop_back();
                    //std::cout << "not " << tmp << std::endl;
                    resStack.push_back(!tmp);
                }
                else if (s == "&"){
                    bool tmp1 = resStack.back();
                    resStack.pop_back();
                    bool tmp2 = resStack.back();
                    resStack.pop_back();
                    //std::cout << tmp1 << " & " << tmp2 << std::endl;
                    resStack.push_back(tmp1 && tmp2);
                }
                else{
                    bool tmp1 = resStack.back();
                    resStack.pop_back();
                    bool tmp2 = resStack.back();
                    resStack.pop_back();
                    //std::cout << tmp1 << " & " << tmp2 << std::endl;
                    resStack.push_back(tmp1 || tmp2);
                }
            }
        }
        if (resStack.size() != 1)
            std::cout << "resStack wrong size" << std::endl;
        return resStack.back();
    }

} // namespace ELHelpers
