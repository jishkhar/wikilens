#include "inverted_index.h"

#include <cstdint>
#include <fstream>

namespace {

template <typename T>
bool writePod(std::ofstream& out, const T& value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(T));
    return static_cast<bool>(out);
}

template <typename T>
bool readPod(std::ifstream& in, T& value) {
    in.read(reinterpret_cast<char*>(&value), sizeof(T));
    return static_cast<bool>(in);
}

bool writeString(std::ofstream& out, const std::string& value) {
    uint64_t len = static_cast<uint64_t>(value.size());
    if (!writePod(out, len)) return false;
    out.write(value.data(), static_cast<std::streamsize>(len));
    return static_cast<bool>(out);
}

bool readString(std::ifstream& in, std::string& value) {
    uint64_t len = 0;
    if (!readPod(in, len)) return false;
    value.resize(static_cast<size_t>(len));
    in.read(value.data(), static_cast<std::streamsize>(len));
    return static_cast<bool>(in);
}

} // namespace

void InvertedIndex::addDocument(
    uint32_t doc_id,
    const std::vector<std::string>& tokens
) {
    for (uint32_t pos = 0; pos < tokens.size(); ++pos) {
        const std::string& term = tokens[pos];
        auto& plist = index_[term];

        if (plist.postings.empty() ||
            plist.postings.back().doc_id != doc_id) {

            Posting p;
            p.doc_id = doc_id;
            p.term_freq = 1;
            p.positions.push_back(pos);

            plist.postings.push_back(p);
            plist.doc_freq++;
        }
        else {
            plist.postings.back().term_freq++;
            plist.postings.back().positions.push_back(pos);
        }
    }

    doc_lengths_[doc_id]  = static_cast<uint32_t>(tokens.size());
    total_token_count_    += tokens.size();
    total_docs_++;
}

const PostingList*
InvertedIndex::lookup(const std::string& term) const {
    auto it = index_.find(term);
    if (it == index_.end()) return nullptr;
    return &it->second;
}

uint32_t InvertedIndex::totalDocs() const {
    return total_docs_;
}

uint32_t InvertedIndex::docLength(uint32_t doc_id) const {
    auto it = doc_lengths_.find(doc_id);
    if (it == doc_lengths_.end()) return 0;
    return it->second;
}

double InvertedIndex::avgDocLength() const {
    if (total_docs_ == 0) return 0.0;
    return static_cast<double>(total_token_count_) / total_docs_;
}

bool InvertedIndex::saveToFile(const std::string& path) const {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;

    constexpr uint32_t kVersion = 1;
    if (!writePod(out, kVersion)) return false;

    uint64_t term_count = static_cast<uint64_t>(index_.size());
    if (!writePod(out, term_count)) return false;

    for (const auto& [term, plist] : index_) {
        if (!writeString(out, term)) return false;
        if (!writePod(out, plist.doc_freq)) return false;

        uint64_t posting_count = static_cast<uint64_t>(plist.postings.size());
        if (!writePod(out, posting_count)) return false;

        for (const auto& posting : plist.postings) {
            if (!writePod(out, posting.doc_id)) return false;
            if (!writePod(out, posting.term_freq)) return false;

            uint64_t pos_count = static_cast<uint64_t>(posting.positions.size());
            if (!writePod(out, pos_count)) return false;

            for (uint32_t pos : posting.positions) {
                if (!writePod(out, pos)) return false;
            }
        }
    }

    uint64_t doc_len_count = static_cast<uint64_t>(doc_lengths_.size());
    if (!writePod(out, doc_len_count)) return false;

    for (const auto& [doc_id, length] : doc_lengths_) {
        if (!writePod(out, doc_id)) return false;
        if (!writePod(out, length)) return false;
    }

    if (!writePod(out, total_docs_)) return false;
    if (!writePod(out, total_token_count_)) return false;

    return static_cast<bool>(out);
}

bool InvertedIndex::loadFromFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;

    uint32_t version = 0;
    if (!readPod(in, version) || version != 1) return false;

    std::unordered_map<std::string, PostingList> new_index;
    std::unordered_map<uint32_t, uint32_t> new_doc_lengths;
    uint32_t new_total_docs = 0;
    uint64_t new_total_token_count = 0;

    uint64_t term_count = 0;
    if (!readPod(in, term_count)) return false;

    for (uint64_t i = 0; i < term_count; ++i) {
        std::string term;
        if (!readString(in, term)) return false;

        PostingList plist;
        if (!readPod(in, plist.doc_freq)) return false;

        uint64_t posting_count = 0;
        if (!readPod(in, posting_count)) return false;

        plist.postings.resize(static_cast<size_t>(posting_count));
        for (uint64_t p = 0; p < posting_count; ++p) {
            Posting posting;
            if (!readPod(in, posting.doc_id)) return false;
            if (!readPod(in, posting.term_freq)) return false;

            uint64_t pos_count = 0;
            if (!readPod(in, pos_count)) return false;

            posting.positions.resize(static_cast<size_t>(pos_count));
            for (uint64_t k = 0; k < pos_count; ++k) {
                if (!readPod(in, posting.positions[static_cast<size_t>(k)])) return false;
            }

            plist.postings[static_cast<size_t>(p)] = std::move(posting);
        }

        new_index.emplace(std::move(term), std::move(plist));
    }

    uint64_t doc_len_count = 0;
    if (!readPod(in, doc_len_count)) return false;

    for (uint64_t i = 0; i < doc_len_count; ++i) {
        uint32_t doc_id = 0;
        uint32_t length = 0;
        if (!readPod(in, doc_id)) return false;
        if (!readPod(in, length)) return false;
        new_doc_lengths[doc_id] = length;
    }

    if (!readPod(in, new_total_docs)) return false;
    if (!readPod(in, new_total_token_count)) return false;

    index_ = std::move(new_index);
    doc_lengths_ = std::move(new_doc_lengths);
    total_docs_ = new_total_docs;
    total_token_count_ = new_total_token_count;

    return true;
}