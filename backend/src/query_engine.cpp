#include "query_engine.h"
#include <queue>
#include <algorithm>

QueryEngine::QueryEngine(InvertedIndex& index,
                         Tokenizer& tokenizer,
                         DocumentStore& store)
    : index_(index),
      tokenizer_(tokenizer),
      store_(store),
      scorer_(index) {}

std::vector<SearchResult>
QueryEngine::search(const std::string& query,
                    size_t limit,
                    size_t offset) {

    auto terms = tokenizer_.tokenize(query);
    auto scores = scorer_.score(terms);

    // Normalize BM25 scores to [0,1]
    double max_bm25 = 0.0;
    for (auto& [doc, score] : scores)
        max_bm25 = std::max(max_bm25, score);

    if (max_bm25 > 0.0) {
        for (auto& [doc, score] : scores)
            score /= max_bm25;
    }

    // Min-heap for Top-K
    auto cmp = [](const SearchResult& a,
                  const SearchResult& b) {
        return a.score > b.score;
    };

    std::priority_queue<
        SearchResult,
        std::vector<SearchResult>,
        decltype(cmp)
    > heap(cmp);

    for (auto& [doc_id, score] : scores) {
        double pr = store_.getPageRank(doc_id);

        double final_score =
            0.85 * score +
            0.15 * pr;

        heap.push({doc_id, final_score});
        if (heap.size() > limit + offset)
            heap.pop();
    }

    std::vector<SearchResult> results;

    while (!heap.empty()) {
        results.push_back(heap.top());
        heap.pop();
    }

    // reverse to get highest first
    std::reverse(results.begin(), results.end());

    // Apply pagination
    if (offset >= results.size()) return {};
    results.erase(results.begin(),
                  results.begin() + static_cast<std::ptrdiff_t>(offset));
    if (results.size() > limit)
        results.resize(limit);

    return results;
}