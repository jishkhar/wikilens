#pragma once
#include <string>
#include <vector>
#include <unordered_set>
#include "porter_stemmer.h"

class Tokenizer {
public:
    Tokenizer(const std::string& stopword_file);
    std::vector<std::string> tokenize(const std::string& text);

private:
    std::unordered_set<std::string> stopwords_;
    PorterStemmer stemmer_;
    void loadStopwords(const std::string& filename);
    bool isStopWord(const std::string& word) const;
};