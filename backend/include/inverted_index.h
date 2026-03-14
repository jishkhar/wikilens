#pragma once
#include <unordered_map>
#include <string>
#include <vector>
#include "posting.h"

class InvertedIndex {
public:
    void addDocument(uint32_t doc_id,
                     const std::vector<std::string>& tokens);

    const PostingList* lookup(const std::string& term) const;

    uint32_t totalDocs() const;
    uint32_t docLength(uint32_t doc_id) const;
    double avgDocLength() const;

    bool saveToFile(const std::string& path) const;
    bool loadFromFile(const std::string& path);

private:
    std::unordered_map<std::string, PostingList> index_;
    std::unordered_map<uint32_t, uint32_t> doc_lengths_;
    uint32_t total_docs_        = 0;
    uint64_t total_token_count_ = 0;  // running sum; makes avgDocLength() O(1)
};