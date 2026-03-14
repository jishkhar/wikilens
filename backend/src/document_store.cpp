#include "document_store.h"

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

void DocumentStore::addDocument(uint32_t doc_id,
                                const std::string& title,
                                const std::string& url,
                                const std::string& snippet) {

    docs_[doc_id] = {doc_id, title, url, snippet};
}

void DocumentStore::setPageRank(uint32_t doc_id, double score) {
    auto it = docs_.find(doc_id);
    if (it != docs_.end()) {
        it->second.pagerank = score;
    }
}

double DocumentStore::getPageRank(uint32_t doc_id) const {
    auto it = docs_.find(doc_id);
    if (it == docs_.end()) return 0.0;
    return it->second.pagerank;
}

const Document*
DocumentStore::getDocument(uint32_t doc_id) const {

    auto it = docs_.find(doc_id);
    if (it == docs_.end()) return nullptr;
    return &it->second;
}

bool DocumentStore::saveToFile(const std::string& path) const {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;

    constexpr uint32_t kVersion = 1;
    if (!writePod(out, kVersion)) return false;

    uint64_t count = static_cast<uint64_t>(docs_.size());
    if (!writePod(out, count)) return false;

    for (const auto& [doc_id, doc] : docs_) {
        if (!writePod(out, doc_id)) return false;
        if (!writeString(out, doc.title)) return false;
        if (!writeString(out, doc.url)) return false;
        if (!writeString(out, doc.snippet)) return false;
        if (!writePod(out, doc.pagerank)) return false;
    }

    return static_cast<bool>(out);
}

bool DocumentStore::loadFromFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;

    uint32_t version = 0;
    if (!readPod(in, version) || version != 1) return false;

    uint64_t count = 0;
    if (!readPod(in, count)) return false;

    std::unordered_map<uint32_t, Document> new_docs;

    for (uint64_t i = 0; i < count; ++i) {
        uint32_t doc_id = 0;
        if (!readPod(in, doc_id)) return false;

        Document doc;
        doc.doc_id = doc_id;

        if (!readString(in, doc.title)) return false;
        if (!readString(in, doc.url)) return false;
        if (!readString(in, doc.snippet)) return false;
        if (!readPod(in, doc.pagerank)) return false;

        new_docs[doc_id] = std::move(doc);
    }

    docs_ = std::move(new_docs);
    return true;
}