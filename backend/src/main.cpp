#define CROW_MAIN
#include "crow.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <unordered_map>
#include <vector>

#include "wiki_parser.h"
#include "wikitext_stripper.h"
#include "tokenizer.h"
#include "inverted_index.h"
#include "document_store.h"
#include "query_engine.h"
#include "pagerank.h"

// Encode a Wikipedia article title into a canonical Wikipedia URL.
// Spaces become underscores; characters that would break a URL are
// percent-encoded; non-ASCII UTF-8 bytes pass through unchanged.
static std::string wikiUrlEncode(const std::string& title) {
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(title.size() + 32);
    for (unsigned char c : title) {
        if (c == ' ') {
            out += '_';
        } else if (c == '%' || c == '#' || c == '?' || c == '&' || c == '=') {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0xF];
        } else {
            out += static_cast<char>(c);
        }
    }
    return out;
}

static std::string wikiUrl(const std::string& title) {
    return "https://en.wikipedia.org/wiki/" + wikiUrlEncode(title);
}

// Return the first ~200 chars of cleaned text, ending on a word boundary.
static std::string snippetFrom(const std::string& text,
                               size_t max_chars = 200) {
    if (text.size() <= max_chars) return text;
    size_t cut = text.rfind(' ', max_chars);
    if (cut == std::string::npos) cut = max_chars;
    std::string out = text.substr(0, cut);
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out + "\xe2\x80\xa6"; // UTF-8 …
}

int main() {

    std::cout << "Starting WikiLens ingestion...\n";

    Tokenizer tokenizer("../data/stopwords.txt");
    WikitextStripper stripper;
    InvertedIndex index;
    DocumentStore store;

    const std::filesystem::path cache_dir = "../data/cache";
    const std::filesystem::path index_cache = cache_dir / "inverted_index.bin";
    const std::filesystem::path store_cache = cache_dir / "document_store.bin";

    bool loaded_from_cache = false;
    if (std::filesystem::exists(index_cache) &&
        std::filesystem::exists(store_cache)) {

        std::cout << "Found cache files. Loading index...\n";
        bool index_ok = index.loadFromFile(index_cache.string());
        bool store_ok = store.loadFromFile(store_cache.string());

        if (index_ok && store_ok) {
            loaded_from_cache = true;
            std::cout << "Loaded cached index and document store.\n";
        } else {
            std::cout << "Cache load failed. Rebuilding from dump...\n";
        }
    }

    if (!loaded_from_cache) {
        std::unordered_map<std::string, uint32_t> title_to_id;
        std::vector<std::vector<std::string>> raw_links;
        std::vector<std::vector<uint32_t>> adjacency_list;

        uint32_t doc_id = 0;

        WikiParser parser;

        parser.parse("../simplewiki-latest-pages-articles.xml.1",
            [&](const std::string& title,
                const std::string& text) {

                StripResult result = stripper.clean(text);

                auto tokens = tokenizer.tokenize(result.cleaned_text);

                index.addDocument(doc_id, tokens);
                store.addDocument(doc_id, title, wikiUrl(title),
                                  snippetFrom(result.cleaned_text));

                title_to_id[title] = doc_id;
                raw_links.push_back(result.internal_links);
                adjacency_list.emplace_back();

                doc_id++;

                if (doc_id % 1000 == 0) {
                    std::cout << "Indexed "
                              << doc_id
                              << " pages\n";
                }

                // For testing only:
                if (doc_id == 20000) {
                    std::cout << "Stopping early at 20k pages.\n";
                    return;
                }
            }
        );

        std::cout << "Building link graph...\n";

        for (uint32_t src = 0; src < raw_links.size(); ++src) {

            for (const std::string& target : raw_links[src]) {

                auto it = title_to_id.find(target);
                if (it != title_to_id.end()) {

                    uint32_t dst = it->second;
                    adjacency_list[src].push_back(dst);
                }
            }
        }

        std::cout << "Computing PageRank...\n";

        PageRank pr(0.85, 30);
        std::vector<double> pr_scores =
            pr.compute(adjacency_list);

        double max_pr = *std::max_element(
            pr_scores.begin(),
            pr_scores.end());

        for (auto& score : pr_scores) {
            score /= max_pr;
            score = std::log(1.0 + score);
        }

        for (uint32_t i = 0; i < pr_scores.size(); ++i) {
            store.setPageRank(i, pr_scores[i]);
        }

        std::filesystem::create_directories(cache_dir);

        bool saved_index = index.saveToFile(index_cache.string());
        bool saved_store = store.saveToFile(store_cache.string());

        if (saved_index && saved_store) {
            std::cout << "Saved cache to " << cache_dir << "\n";
        } else {
            std::cout << "Warning: failed to save cache files.\n";
        }
    }

    std::cout << "Ingestion complete.\n";
    std::cout << "Total documents: "
              << index.totalDocs() << "\n";

    QueryEngine engine(index, tokenizer, store);

    crow::SimpleApp app;

    CROW_ROUTE(app, "/")
    ([](){
        return "WikiLens Search Engine Running";
    });

    CROW_ROUTE(app, "/search")
    ([&](const crow::request& req){

        crow::response res;
        res.add_header("Access-Control-Allow-Origin", "*");

        auto query = req.url_params.get("q");
        if (!query) {
            res.code = 400;
            res.body = "Missing query parameter";
            return res;
        }

        size_t limit  = 10;
        size_t offset = 0;
        if (const char* l = req.url_params.get("limit"))
            limit  = static_cast<size_t>(std::stoul(l));
        if (const char* o = req.url_params.get("offset"))
            offset = static_cast<size_t>(std::stoul(o));

        auto results = engine.search(query, limit, offset);

        crow::json::wvalue response;
        response["results"] = crow::json::wvalue::list();

        for (size_t i = 0; i < results.size(); ++i) {

            const Document* doc =
                store.getDocument(results[i].doc_id);

            if (!doc) continue;

            crow::json::wvalue item;
            item["title"]   = doc->title;
            item["url"]     = doc->url;
            item["snippet"] = doc->snippet;
            item["score"]   = results[i].score;
            item["pagerank"] = doc->pagerank;

            response["results"][i] = std::move(item);
        }

        res.write(response.dump());
        return res;
    });

    std::cout << "Starting HTTP server on port 8080...\n";

    app.port(8080).multithreaded().run();

    return 0;
}