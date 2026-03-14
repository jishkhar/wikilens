#include "wiki_parser.h"
#include <expat.h>
#include <fstream>
#include <iostream>
#include <vector>

struct ParserState {
    std::string current_element;
    std::string title;
    std::string text;
    bool in_page = false;
    bool in_revision = false;
    WikiParser::PageCallback callback;
};

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

            state->callback(state->title,
                            state->text);
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

void WikiParser::parse(
    const std::string& xml_file,
    PageCallback callback) {

    std::ifstream file(xml_file,
                       std::ios::binary);

    if (!file) {
        std::cerr << "Cannot open file\n";
        return;
    }

    XML_Parser parser =
        XML_ParserCreate(NULL);

    ParserState state;
    state.callback = callback;

    XML_SetUserData(parser, &state);
    XML_SetElementHandler(parser,
                          startElement,
                          endElement);
    XML_SetCharacterDataHandler(parser,
                                charData);

    const int buffer_size = 8192;
    std::vector<char> buffer(buffer_size);

    while (file) {
        file.read(buffer.data(), buffer_size);
        std::streamsize bytes_read =
            file.gcount();

        if (!XML_Parse(parser,
                       buffer.data(),
                       bytes_read,
                       file.eof())) {

            std::cerr << "XML Parse Error\n";
            break;
        }
    }

    XML_ParserFree(parser);
}