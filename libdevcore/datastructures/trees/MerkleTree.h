//
// Created by Kirill Zhukov on 27.06.2022.
//

#ifndef UNIT_MERKLETREE_H
#define UNIT_MERKLETREE_H
#include "iostream"
#include "vector"
#include "string"
#include "cmath"
#include "optional"
#include "../../crypto/SHA3/sha3.h"

class MerkleTree {
public:
    MerkleTree();

    explicit MerkleTree(const std::vector<std::string> &initialArray);

    static inline std::string concat(const std::string *psl,
                             const std::string *psr) noexcept {
        if (!(psl || psr)) return "";
        return !psl ? *psr : !psr ? *psl : *psl + *psr;
    }

    void buildTree(unsigned long long currentIndex, unsigned long long leftQueryBorder, unsigned long long rightQueryBorder);

    void print_tree();
    inline std::optional<std::string> get_root(){
        if (this->tree.empty() || this->initial_array.empty())
            return std::nullopt;
        return tree[0];
    }

    [[nodiscard]] const std::vector<std::string> *getInitialArray() const;
    void setInitialArray(const std::vector<std::string> &initialArray);

private:
    std::vector<std::string> initial_array;
    std::vector<std::string> tree;
    SHA3 sha3 = SHA3(SHA3::Bits256);
};

#endif //UNIT_MERKLETREE_H