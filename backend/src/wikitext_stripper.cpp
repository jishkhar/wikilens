#include "wikitext_stripper.h"

#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace {

bool startsWithAt(const std::string& text,
                  size_t pos,
                  const std::string& prefix) {
    return pos + prefix.size() <= text.size() &&
           text.compare(pos, prefix.size(), prefix) == 0;
}

bool isSpaceChar(unsigned char c) {
    return std::isspace(c) != 0;
}

std::string trim(std::string value) {
    size_t start = 0;
    while (start < value.size() &&
           isSpaceChar(static_cast<unsigned char>(value[start]))) {
        ++start;
    }

    size_t end = value.size();
    while (end > start &&
           isSpaceChar(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }

    return value.substr(start, end - start);
}

size_t findMatchingDoubleClose(const std::string& text,
                               size_t open_pos,
                               const std::string& open_token,
                               const std::string& close_token) {
    int depth = 0;

    for (size_t i = open_pos; i < text.size(); ++i) {
        if (startsWithAt(text, i, open_token)) {
            ++depth;
            ++i;
            continue;
        }

        if (startsWithAt(text, i, close_token)) {
            --depth;
            if (depth == 0) {
                return i;
            }
            ++i;
        }
    }

    return std::string::npos;
}

} // namespace

StripResult WikitextStripper::clean(const std::string& text) {
    std::string output = text;

    output = stripComments(output);
    output = stripRefTags(output);
    output = stripTemplates(output);
    output = stripTables(output);

    StripResult link_result = processLinks(output);

    std::string cleaned = stripFormatting(link_result.cleaned_text);
    cleaned = collapseWhitespace(cleaned);

    return {cleaned, link_result.internal_links};
}

std::string WikitextStripper::stripComments(const std::string& text) {
    std::string result;
    size_t pos = 0;

    while (pos < text.size()) {
        size_t open = text.find("<!--", pos);
        if (open == std::string::npos) {
            result.append(text, pos, std::string::npos);
            break;
        }

        result.append(text, pos, open - pos);

        size_t close = text.find("-->", open + 4);
        if (close == std::string::npos) {
            break;
        }

        pos = close + 3;
    }

    return result;
}

std::string WikitextStripper::stripRefTags(const std::string& text) {
    std::string result;
    size_t pos = 0;

    while (pos < text.size()) {
        size_t open = text.find("<ref", pos);
        if (open == std::string::npos) {
            result.append(text, pos, std::string::npos);
            break;
        }

        result.append(text, pos, open - pos);

        size_t tag_end = text.find('>', open + 4);
        if (tag_end == std::string::npos) {
            break;
        }

        if (tag_end > open && text[tag_end - 1] == '/') {
            pos = tag_end + 1;
            continue;
        }

        size_t close = text.find("</ref>", tag_end + 1);
        if (close == std::string::npos) {
            pos = tag_end + 1;
            continue;
        }

        pos = close + 6;
    }

    return result;
}

std::string WikitextStripper::stripTemplates(const std::string& text) {
    std::string result;
    size_t pos = 0;

    while (pos < text.size()) {
        size_t open = text.find("{{", pos);
        if (open == std::string::npos) {
            result.append(text, pos, std::string::npos);
            break;
        }

        result.append(text, pos, open - pos);

        size_t close = findMatchingDoubleClose(text, open, "{{", "}}");
        if (close == std::string::npos) {
            break;
        }

        pos = close + 2;
    }

    return result;
}

std::string WikitextStripper::stripTables(const std::string& text) {
    std::string result;
    size_t pos = 0;

    while (pos < text.size()) {
        size_t open = text.find("{|", pos);
        if (open == std::string::npos) {
            result.append(text, pos, std::string::npos);
            break;
        }

        result.append(text, pos, open - pos);

        size_t close = findMatchingDoubleClose(text, open, "{|", "|}");
        if (close == std::string::npos) {
            break;
        }

        pos = close + 2;
    }

    return result;
}

StripResult WikitextStripper::processLinks(const std::string& text) {
    std::string result;
    std::vector<std::string> links;

    for (size_t i = 0; i < text.size();) {
        if (startsWithAt(text, i, "[[")) {
            size_t close = text.find("]]", i + 2);
            if (close == std::string::npos) {
                result += text[i++];
                continue;
            }

            std::string inner = text.substr(i + 2, close - (i + 2));
            std::string target = inner;
            std::string display = inner;

            size_t pipe = inner.find('|');
            if (pipe != std::string::npos) {
                target = inner.substr(0, pipe);
                display = inner.substr(pipe + 1);

                size_t last_pipe = display.rfind('|');
                if (last_pipe != std::string::npos) {
                    display = display.substr(last_pipe + 1);
                }
            }

            std::string normalized_target = normalizeLinkTarget(target);
            std::string cleaned_display = cleanLinkDisplay(display);

            if (cleaned_display.empty()) {
                cleaned_display = cleanLinkDisplay(target);
            }

            if (shouldRecordInternalLink(normalized_target)) {
                links.push_back(normalized_target);
            }

            result += cleaned_display;
            i = close + 2;
            continue;
        }

        if (text[i] == '[' &&
            (i + 1 >= text.size() || text[i + 1] != '[')) {
            size_t close = text.find(']', i + 1);
            if (close == std::string::npos) {
                result += text[i++];
                continue;
            }

            std::string inner = trim(text.substr(i + 1, close - (i + 1)));
            size_t split = inner.find_first_of(" \t\n");
            std::string link = split == std::string::npos
                ? inner
                : inner.substr(0, split);
            std::string label = split == std::string::npos
                ? std::string()
                : trim(inner.substr(split + 1));

            if (link.rfind("http://", 0) == 0 ||
                link.rfind("https://", 0) == 0 ||
                link.rfind("ftp://", 0) == 0 ||
                link.rfind("mailto:", 0) == 0) {
                result += cleanLinkDisplay(label);
                i = close + 1;
                continue;
            }
        }

        result += text[i];
        ++i;
    }

    return {result, links};
}

std::string WikitextStripper::stripFormatting(const std::string& text) {
    std::string without_quotes;
    without_quotes.reserve(text.size());

    for (size_t i = 0; i < text.size();) {
        if (text[i] == '\'') {
            size_t j = i;
            while (j < text.size() && text[j] == '\'') {
                ++j;
            }

            if (j - i >= 2) {
                i = j;
                continue;
            }
        }

        without_quotes += text[i];
        ++i;
    }

    std::string result;
    size_t line_start = 0;

    while (line_start <= without_quotes.size()) {
        size_t line_end = without_quotes.find('\n', line_start);
        if (line_end == std::string::npos) {
            line_end = without_quotes.size();
        }

        std::string line = without_quotes.substr(line_start, line_end - line_start);
        std::string trimmed_line = trim(line);

        if (!trimmed_line.empty()) {
            size_t left = 0;
            while (left < trimmed_line.size() && trimmed_line[left] == '=') {
                ++left;
            }

            size_t right = trimmed_line.size();
            while (right > left && trimmed_line[right - 1] == '=') {
                --right;
            }

            if (left >= 2 && trimmed_line.size() - right >= 2) {
                trimmed_line = trim(trimmed_line.substr(left, right - left));
            }

            result += trimmed_line;
            result += '\n';
        } else {
            result += '\n';
        }

        if (line_end == without_quotes.size()) {
            break;
        }
        line_start = line_end + 1;
    }

    return result;
}

std::string WikitextStripper::collapseWhitespace(const std::string& text) {
    std::string result;
    result.reserve(text.size());

    bool prev_space = true;

    for (unsigned char c : text) {
        if (isSpaceChar(c)) {
            if (!prev_space) {
                result += ' ';
                prev_space = true;
            }
            continue;
        }

        result += static_cast<char>(c);
        prev_space = false;
    }

    if (!result.empty() && result.back() == ' ') {
        result.pop_back();
    }

    return result;
}

std::string WikitextStripper::cleanLinkDisplay(const std::string& raw_display) {
    std::string display = raw_display;

    size_t anchor = display.find('#');
    if (anchor != std::string::npos && anchor == 0) {
        display.erase(anchor, 1);
    }

    std::replace(display.begin(), display.end(), '_', ' ');
    return trim(display);
}

std::string WikitextStripper::normalizeLinkTarget(const std::string& raw_target) {
    std::string target = trim(raw_target);
    if (target.empty()) {
        return {};
    }

    std::replace(target.begin(), target.end(), '_', ' ');

    if (!target.empty() && target[0] == ':') {
        target.erase(0, 1);
        target = trim(target);
    }

    size_t anchor = target.find('#');
    if (anchor != std::string::npos) {
        target = trim(target.substr(0, anchor));
    }

    return target;
}

bool WikitextStripper::shouldRecordInternalLink(const std::string& target) const {
    if (target.empty()) {
        return false;
    }

    size_t colon = target.find(':');
    if (colon == std::string::npos) {
        return true;
    }

    std::string prefix = target.substr(0, colon);
    std::transform(prefix.begin(), prefix.end(), prefix.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });

    static const std::unordered_set<std::string> ignored_namespaces = {
        "category", "file", "image", "help", "portal", "special",
        "template", "wikipedia", "user", "draft", "module", "media"
    };

    return ignored_namespaces.find(prefix) == ignored_namespaces.end();
}
