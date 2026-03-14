#pragma once
#include <vector>
#include <string>
#include "inverted_index.h"
#include "bm25.h"
#include "tokenizer.h"
#include "document_store.h"

struct SearchResult {
    uint32_t doc_id;
    double score;
};

class QueryEngine {
public:
    QueryEngine(InvertedIndex& index,
                Tokenizer& tokenizer,
                DocumentStore& store);

    std::vector<SearchResult>
    search(const std::string& query,
           size_t limit  = 10,
           size_t offset = 0);

private:
    InvertedIndex& index_;
    Tokenizer& tokenizer_;
    DocumentStore& store_;
    BM25 scorer_;
};