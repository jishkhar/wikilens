#pragma once
#include <string>

class PorterStemmer {
public:
    std::string stem(const std::string& word);

private:
    bool isVowel(const std::string& word, int i);
    int measure(const std::string& word);
    bool containsVowel(const std::string& word);
    bool endsWithDoubleConsonant(const std::string& word);
    bool cvc(const std::string& word);
    bool endsWith(const std::string& word, const std::string& suffix);
    void replaceSuffix(std::string& word, const std::string& suffix, const std::string& replacement);
};