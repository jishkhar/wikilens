#include "wiki_parser.h"

#include <bzlib.h>
#include <expat.h>

#include <cstdio>
#include <fstream>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

struct ParserState {
    std::string current_element;
    std::string title;
    std::string text;
    bool in_page = false;
    bool in_revision = false;
    bool stop_requested = false;
    XML_Parser parser = nullptr;
    WikiParser::PageCallback callback;
};

bool endsWith(std::string_view value,
              std::string_view suffix) {
    return value.size() >= suffix.size() &&
           value.substr(value.size() - suffix.size()) == suffix;
}

bool parseChunk(XML_Parser parser,
                ParserState& state,
                const char* data,
                int size,
                bool is_final_chunk) {
    if (XML_Parse(parser, data, size, is_final_chunk) != XML_STATUS_ERROR) {
        return true;
    }

    if (state.stop_requested &&
        XML_GetErrorCode(parser) == XML_ERROR_ABORTED) {
        return true;
    }

    std::cerr << "XML Parse Error: "
              << XML_ErrorString(XML_GetErrorCode(parser))
              << "\n";
    return false;
}

bool parsePlainXml(const std::string& xml_file,
                   XML_Parser parser,
                   ParserState& state) {
    std::ifstream file(xml_file, std::ios::binary);
    if (!file) {
        std::cerr << "Cannot open dump file: "
                  << xml_file << "\n";
        return false;
    }

    constexpr int buffer_size = 8192;
    std::vector<char> buffer(buffer_size);

    while (file) {
        file.read(buffer.data(), buffer_size);
        std::streamsize bytes_read = file.gcount();

        if (!parseChunk(parser,
                        state,
                        buffer.data(),
                        static_cast<int>(bytes_read),
                        file.eof())) {
            return false;
        }

        if (state.stop_requested) {
            return true;
        }
    }

    return true;
}

bool parseCompressedXml(const std::string& xml_file,
                        XML_Parser parser,
                        ParserState& state) {
    std::FILE* file = std::fopen(xml_file.c_str(), "rb");
    if (!file) {
        std::cerr << "Cannot open dump file: "
                  << xml_file << "\n";
        return false;
    }

    int bz_error = BZ_OK;
    BZFILE* bz_file = BZ2_bzReadOpen(&bz_error, file, 0, 0, nullptr, 0);
    if (bz_error != BZ_OK) {
        std::cerr << "Failed to open bz2 stream for: "
                  << xml_file << "\n";
        std::fclose(file);
        return false;
    }

    constexpr int buffer_size = 8192;
    std::vector<char> buffer(buffer_size);
    bool ok = true;

    while (true) {
        int bytes_read =
            BZ2_bzRead(&bz_error, bz_file, buffer.data(), buffer_size);

        if (bz_error != BZ_OK && bz_error != BZ_STREAM_END) {
            std::cerr << "bz2 read error while parsing: "
                      << xml_file << "\n";
            ok = false;
            break;
        }

        const bool is_final_chunk = (bz_error == BZ_STREAM_END);
        ok = parseChunk(parser, state, buffer.data(), bytes_read, is_final_chunk);
        if (!ok || state.stop_requested || is_final_chunk) {
            break;
        }
    }

    BZ2_bzReadClose(&bz_error, bz_file);
    std::fclose(file);
    return ok;
}

} // namespace

static void startElement(void* userData,
                         const char* name,
                         const char**) {
    ParserState* state =
        static_cast<ParserState*>(userData);

    state->current_element = name;

    if (state->current_element == "page") {
        state->in_page = true;
        state->title.clear();
        state->text.clear();
    }

    if (state->current_element == "revision") {
        state->in_revision = true;
    }
}

static void endElement(void* userData,
                       const char* name) {
    ParserState* state =
        static_cast<ParserState*>(userData);

    if (std::string(name) == "page") {
        if (!state->title.empty() &&
            !state->text.empty()) {

            bool should_continue =
                state->callback(state->title,
                                state->text);
            if (!should_continue) {
                state->stop_requested = true;
                XML_StopParser(state->parser, XML_FALSE);
            }
        }
        state->in_page = false;
    }

    if (std::string(name) == "revision") {
        state->in_revision = false;
    }

    state->current_element.clear();
}

static void charData(void* userData,
                     const char* s,
                     int len) {
    ParserState* state =
        static_cast<ParserState*>(userData);

    if (!state->in_page) return;

    if (state->current_element == "title") {
        state->title.append(s, len);
    }

    if (state->current_element == "text" &&
        state->in_revision) {
        state->text.append(s, len);
    }
}

bool WikiParser::parse(
    const std::string& xml_file,
    PageCallback callback) {
    XML_Parser parser = XML_ParserCreate(NULL);
    if (!parser) {
        std::cerr << "Failed to create XML parser\n";
        return false;
    }

    ParserState state;
    state.callback = callback;
    state.parser = parser;

    XML_SetUserData(parser, &state);
    XML_SetElementHandler(parser,
                          startElement,
                          endElement);
    XML_SetCharacterDataHandler(parser,
                                charData);

    const bool is_bz2 = endsWith(xml_file, ".bz2");
    const bool ok = is_bz2
        ? parseCompressedXml(xml_file, parser, state)
        : parsePlainXml(xml_file, parser, state);

    XML_ParserFree(parser);
    return ok;
}
