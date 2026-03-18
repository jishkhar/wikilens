#pragma once
#include <string>
#include <functional>

class WikiParser {
public:
    using PageCallback =
        std::function<bool(const std::string& title,
                           const std::string& text)>;

    bool parse(const std::string& xml_file,
               PageCallback callback);
};
