#pragma once
#include <cstdint>
#include <vector>

struct Posting {
    uint32_t doc_id;
    uint32_t term_freq;
    std::vector<uint32_t> positions;  // for future phrase queries
};

struct PostingList {
    uint32_t doc_freq = 0;
    std::vector<Posting> postings;
};