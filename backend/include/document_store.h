#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>

struct Document {
    uint32_t doc_id;
    std::string title;
    std::string url;
    std::string snippet;
    double pagerank = 0.0;
};

class DocumentStore {
public:
    void addDocument(uint32_t doc_id,
                     const std::string& title,
                     const std::string& url,
                     const std::string& snippet);

    void setPageRank(uint32_t doc_id, double score);
    double getPageRank(uint32_t doc_id) const;

    const Document* getDocument(uint32_t doc_id) const;

    bool saveToFile(const std::string& path) const;
    bool loadFromFile(const std::string& path);

private:
    std::unordered_map<uint32_t, Document> docs_;
};