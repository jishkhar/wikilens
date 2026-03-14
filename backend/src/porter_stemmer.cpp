#include "porter_stemmer.h"
#include <cctype>

// Utility functions

bool PorterStemmer::isVowel(const std::string& word, int i) {
    char c = word[i];
    if (c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u')
        return true;
    if (c == 'y') {
        if (i == 0) return false;
        return !isVowel(word, i - 1);
    }
    return false;
}

int PorterStemmer::measure(const std::string& word) {
    int count = 0;
    bool prevVowel = false;

    for (int i = 0; i < (int)word.size(); ++i) {
        bool currVowel = isVowel(word, i);
        if (prevVowel && !currVowel)
            count++;
        prevVowel = currVowel;
    }
    return count;
}

bool PorterStemmer::containsVowel(const std::string& word) {
    for (int i = 0; i < (int)word.size(); ++i)
        if (isVowel(word, i)) return true;
    return false;
}

bool PorterStemmer::endsWithDoubleConsonant(const std::string& word) {
    int n = word.size();
    if (n < 2) return false;
    return word[n-1] == word[n-2] && !isVowel(word, n-1);
}

bool PorterStemmer::cvc(const std::string& word) {
    int n = word.size();
    if (n < 3) return false;
    if (!isVowel(word, n-1) &&
        isVowel(word, n-2) &&
        !isVowel(word, n-3)) {

        char c = word[n-1];
        if (c != 'w' && c != 'x' && c != 'y')
            return true;
    }
    return false;
}

bool PorterStemmer::endsWith(const std::string& word, const std::string& suffix) {
    if (word.length() < suffix.length())
        return false;
    return word.compare(word.length() - suffix.length(), suffix.length(), suffix) == 0;
}

void PorterStemmer::replaceSuffix(std::string& word,
                                  const std::string& suffix,
                                  const std::string& replacement) {
    word.replace(word.length() - suffix.length(), suffix.length(), replacement);
}

std::string PorterStemmer::stem(const std::string& input) {
    if (input.length() <= 2)
        return input;

    std::string word = input;

    // Step 1a
    if (endsWith(word, "sses"))
        replaceSuffix(word, "sses", "ss");
    else if (endsWith(word, "ies"))
        replaceSuffix(word, "ies", "i");
    else if (endsWith(word, "ss"))
        ; // do nothing
    else if (endsWith(word, "s"))
        replaceSuffix(word, "s", "");

    // Step 1b
    if (endsWith(word, "eed")) {
        std::string stem = word.substr(0, word.length() - 3);
        if (measure(stem) > 0)
            replaceSuffix(word, "eed", "ee");
    }
    else if ((endsWith(word, "ed") &&
             containsVowel(word.substr(0, word.length() - 2)))) {

        word = word.substr(0, word.length() - 2);

        if (endsWith(word, "at") || endsWith(word, "bl") || endsWith(word, "iz"))
            word += "e";
        else if (endsWithDoubleConsonant(word) &&
                 !(word.back() == 'l' || word.back() == 's' || word.back() == 'z'))
            word.pop_back();
        else if (measure(word) == 1 && cvc(word))
            word += "e";
    }
    else if ((endsWith(word, "ing") &&
             containsVowel(word.substr(0, word.length() - 3)))) {

        word = word.substr(0, word.length() - 3);

        if (endsWith(word, "at") || endsWith(word, "bl") || endsWith(word, "iz"))
            word += "e";
        else if (endsWithDoubleConsonant(word) &&
                 !(word.back() == 'l' || word.back() == 's' || word.back() == 'z'))
            word.pop_back();
        else if (measure(word) == 1 && cvc(word))
            word += "e";
    }

    // Step 1c
    if (endsWith(word, "y") &&
        containsVowel(word.substr(0, word.length() - 1)))
        word[word.length() - 1] = 'i';

    return word;
}