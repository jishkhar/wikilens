#include "wikitext_stripper.h"

StripResult WikitextStripper::clean(const std::string& text) {

    std::string output = text;

    output = stripComments(output);
    output = stripRefTags(output);
    output = stripTemplates(output);
    output = stripTables(output);

    StripResult link_result = processLinks(output);

    std::string cleaned =
        stripFormatting(link_result.cleaned_text);

    return {cleaned, link_result.internal_links};
}

std::string WikitextStripper::stripComments(const std::string& text) {
    std::string result;
    bool in_comment = false;

    for (size_t i = 0; i < text.size(); ++i) {

        if (!in_comment &&
            i + 3 < text.size() &&
            text.substr(i, 4) == "<!--") {
            in_comment = true;
            i += 3;
            continue;
        }

        if (in_comment &&
            i + 2 < text.size() &&
            text.substr(i, 3) == "-->") {
            in_comment = false;
            i += 2;
            continue;
        }

        if (!in_comment)
            result += text[i];
    }

    return result;
}

std::string WikitextStripper::stripTemplates(const std::string& text) {

    std::string result;
    int depth = 0;

    for (size_t i = 0; i < text.size(); ++i) {

        if (i + 1 < text.size() &&
            text[i] == '{' && text[i+1] == '{') {
            depth++;
            i++;
            continue;
        }

        if (i + 1 < text.size() &&
            text[i] == '}' && text[i+1] == '}') {
            if (depth > 0) depth--;
            i++;
            continue;
        }

        if (depth == 0)
            result += text[i];
    }

    return result;
}

std::string WikitextStripper::stripRefTags(const std::string& text) {

    std::string result;
    bool in_ref = false;

    for (size_t i = 0; i < text.size(); ++i) {

        if (!in_ref &&
            i + 4 < text.size() &&
            text.substr(i, 4) == "<ref") {
            in_ref = true;
            continue;
        }

        if (in_ref &&
            i + 5 < text.size() &&
            text.substr(i, 6) == "</ref>") {
            in_ref = false;
            i += 5;
            continue;
        }

        if (!in_ref)
            result += text[i];
    }

    return result;
}

std::string WikitextStripper::stripTables(const std::string& text) {

    std::string result;
    bool in_table = false;

    for (size_t i = 0; i < text.size(); ++i) {

        if (!in_table &&
            i + 1 < text.size() &&
            text[i] == '{' && text[i+1] == '|') {
            in_table = true;
            i++;
            continue;
        }

        if (in_table &&
            i + 1 < text.size() &&
            text[i] == '|' && text[i+1] == '}') {
            in_table = false;
            i++;
            continue;
        }

        if (!in_table)
            result += text[i];
    }

    return result;
}

StripResult WikitextStripper::processLinks(const std::string& text) {

    std::string result;
    std::vector<std::string> links;

    for (size_t i = 0; i < text.size(); ++i) {

        // Internal link [[...]]
        if (i + 1 < text.size() &&
            text[i] == '[' && text[i+1] == '[') {

            i += 2;
            std::string link_target;
            std::string display_text;

            while (i < text.size() &&
                   !(text[i] == ']' && text[i+1] == ']')) {

                if (text[i] == '|') {
                    display_text.clear();
                    i++;
                    continue;
                }

                link_target += text[i];
                display_text += text[i];
                i++;
            }

            links.push_back(link_target);
            result += display_text;

            i++;
            continue;
        }

        result += text[i];
    }

    return {result, links};
}

std::string WikitextStripper::stripFormatting(const std::string& text) {

    std::string result;
    size_t i = 0;

    while (i < text.size()) {

        // Strip bold+italic: '''''
        if (i + 4 < text.size() && text.substr(i, 5) == "'''''") {
            i += 5;
            continue;
        }

        // Strip bold: '''
        if (i + 2 < text.size() && text.substr(i, 3) == "'''") {
            i += 3;
            continue;
        }

        // Strip italic: ''
        if (i + 1 < text.size() && text.substr(i, 2) == "''") {
            i += 2;
            continue;
        }

        // Strip heading markers (== ... ==) — replace with a space
        if (text[i] == '=') {
            while (i < text.size() && text[i] == '=')
                i++;
            result += ' ';
            continue;
        }

        result += text[i];
        i++;
    }

    return result;
}
