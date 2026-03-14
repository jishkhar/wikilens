#include "tokenizer.h"
#include <fstream>
#include <cctype>

Tokenizer::Tokenizer(const std::string& stopword_file) {
    loadStopwords(stopword_file);
}

void Tokenizer::loadStopwords(const std::string& filename) {
    std::ifstream file(filename);
    std::string word;
    while (file >> word) {
        stopwords_.insert(word);
    }
}

bool Tokenizer::isStopWord(const std::string& word) const {
    return stopwords_.find(word) != stopwords_.end();
}

std::vector<std::string> Tokenizer::tokenize(const std::string& text) {
    std::vector<std::string> tokens;
    std::string current;

    for (char c : text) {
        if (std::isalnum(c)) {
            current += std::tolower(c);
        } else {
            if (!current.empty()) {
                if (!isStopWord(current)) {
                    tokens.push_back(stemmer_.stem(current));
                }
                current.clear();
            }
        }
    }

    if (!current.empty() && !isStopWord(current)) {
        tokens.push_back(stemmer_.stem(current));
    }

    return tokens;
}