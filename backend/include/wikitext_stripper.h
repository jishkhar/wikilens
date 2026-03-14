#pragma once
#include <string>
#include <vector>

struct StripResult {
    std::string cleaned_text;
    std::vector<std::string> internal_links;
};

class WikitextStripper {
public:
    StripResult clean(const std::string& text);

private:
    std::string stripComments(const std::string& text);
    std::string stripRefTags(const std::string& text);
    std::string stripTemplates(const std::string& text);
    std::string stripTables(const std::string& text);
    std::string stripFormatting(const std::string& text);

    StripResult processLinks(const std::string& text);
};