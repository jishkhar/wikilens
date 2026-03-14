#include "bm25.h"
#include <cmath>

BM25::BM25(const InvertedIndex& index)
    : index_(index) {}

std::unordered_map<uint32_t, double>
BM25::score(const std::vector<std::string>& query_terms) {

    std::unordered_map<uint32_t, double> scores;

    uint32_t N = index_.totalDocs();
    double avgDL = index_.avgDocLength();

    for (const auto& term : query_terms) {

        const PostingList* plist = index_.lookup(term);
        if (!plist) continue;

        double df = plist->doc_freq;
        double idf = std::log((N - df + 0.5) / (df + 0.5) + 1.0);

        for (const auto& posting : plist->postings) {

            double tf = posting.term_freq;
            double dl = index_.docLength(posting.doc_id);

            double norm = 1.0 - b + b * (dl / avgDL);

            double tf_component =
                (tf * (k1 + 1.0)) /
                (tf + k1 * norm);

            scores[posting.doc_id] += idf * tf_component;
        }
    }

    return scores;
}