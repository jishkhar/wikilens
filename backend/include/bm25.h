#pragma once
#include <unordered_map>
#include <vector>
#include <string>
#include "inverted_index.h"

class BM25 {
public:
    BM25(const InvertedIndex& index);

    std::unordered_map<uint32_t, double>
    score(const std::vector<std::string>& query_terms);

private:
    const InvertedIndex& index_;
    static constexpr double k1 = 1.5;
    static constexpr double b  = 0.75;
};