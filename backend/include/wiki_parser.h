#pragma once
#include <string>
#include <functional>

class WikiParser {
public:
    using PageCallback =
        std::function<void(const std::string& title,
                           const std::string& text)>;

    void parse(const std::string& xml_file,
               PageCallback callback);
};