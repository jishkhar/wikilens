#define CROW_MAIN
#include "crow.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <optional>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "document_store.h"
#include "inverted_index.h"
#include "lru_cache.h"
#include "pagerank.h"
#include "query_engine.h"
#include "tokenizer.h"
#include "wiki_parser.h"
#include "wikitext_stripper.h"

namespace {

using Clock = std::chrono::steady_clock;
using Nanoseconds = std::chrono::nanoseconds;

constexpr std::string_view kDefaultDumpPath =
    "../simplewiki-latest-pages-articles.xml.1";
constexpr size_t kDefaultLimit = 10;
constexpr size_t kMaxLimit = 100;
constexpr size_t kDefaultSuggestLimit = 8;
constexpr size_t kMaxSuggestLimit = 12;
constexpr size_t kMaxQueryLength = 512;
constexpr size_t kSearchCacheCapacity = 1000;

struct RuntimeConfig {
    std::filesystem::path dump_path = std::filesystem::path(kDefaultDumpPath);
    bool dump_path_overridden = false;
    size_t max_docs = 0;
    bool compute_pagerank_during_index = false;
    bool recompute_pagerank_only = false;
    bool benchmark_only = false;
    bool profile_indexing = false;
    std::filesystem::path benchmark_queries_path;
};

struct ArtifactPaths {
    std::filesystem::path cache_dir;
    std::filesystem::path index_cache;
    std::filesystem::path store_cache;
    std::filesystem::path link_graph;
};

struct SearchCacheKey {
    std::string query;
    size_t limit = 0;
    size_t offset = 0;

    bool operator==(const SearchCacheKey& other) const {
        return query == other.query &&
               limit == other.limit &&
               offset == other.offset;
    }
};

struct SearchCacheKeyHash {
    size_t operator()(const SearchCacheKey& key) const {
        size_t h1 = std::hash<std::string>{}(key.query);
        size_t h2 = std::hash<size_t>{}(key.limit);
        size_t h3 = std::hash<size_t>{}(key.offset);
        return h1 ^ (h2 << 1) ^ (h3 << 2);
    }
};

struct SearchCacheStats {
    uint64_t hits = 0;
    uint64_t misses = 0;
};

struct IndexingProfile {
    bool enabled = false;
    size_t docs_processed = 0;
    Nanoseconds strip_time{0};
    Nanoseconds tokenize_time{0};
    Nanoseconds index_time{0};

    void print() const {
        if (!enabled || docs_processed == 0) {
            return;
        }

        auto toMs = [](Nanoseconds value) {
            return static_cast<double>(value.count()) / 1'000'000.0;
        };

        const double docs = static_cast<double>(docs_processed);
        std::cout << "Indexing profile over " << docs_processed << " docs:\n";
        std::cout << "  Strip Wikitext: " << std::fixed << std::setprecision(2)
                  << toMs(strip_time) << " ms total, "
                  << (toMs(strip_time) / docs) << " ms/doc\n";
        std::cout << "  Tokenize: " << toMs(tokenize_time) << " ms total, "
                  << (toMs(tokenize_time) / docs) << " ms/doc\n";
        std::cout << "  Index + Store: " << toMs(index_time) << " ms total, "
                  << (toMs(index_time) / docs) << " ms/doc\n";
    }
};

void printUsage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [options]\n"
              << "Options:\n"
              << "  --dump <path>               Override the Wikipedia dump path\n"
              << "  --max-docs <count>          Stop indexing after N documents (0 = unlimited)\n"
              << "  --compute-pagerank          Compute PageRank during indexing instead of deferring it\n"
              << "  --recompute-pagerank        Load cached store + edge list, compute PageRank, then exit\n"
              << "  --benchmark                 Run query-latency benchmarks against the loaded index, then exit\n"
              << "  --benchmark-queries <path>  Read benchmark queries from a text file\n"
              << "  --profile-indexing          Print strip/tokenize/index timing totals during ingestion\n"
              << "Environment variables:\n"
              << "  WIKILENS_DUMP_PATH          Override the Wikipedia dump path\n"
              << "  WIKILENS_MAX_DOCS           Stop indexing after N documents (0 = unlimited)\n"
              << "  WIKILENS_COMPUTE_PAGERANK   Set to 1 to compute PageRank during indexing\n";
}

std::optional<size_t> parseSizeT(std::string_view value) {
    if (value.empty()) {
        return std::nullopt;
    }

    size_t parsed = 0;
    auto [ptr, ec] = std::from_chars(value.data(),
                                     value.data() + value.size(),
                                     parsed);
    if (ec != std::errc() || ptr != value.data() + value.size()) {
        return std::nullopt;
    }

    return parsed;
}

bool parseBoolEnv(const char* value) {
    if (!value) {
        return false;
    }

    std::string_view raw(value);
    return raw == "1" || raw == "true" || raw == "TRUE" || raw == "yes";
}

bool hasNonWhitespace(std::string_view value) {
    for (unsigned char ch : value) {
        if (std::isspace(ch) == 0) {
            return true;
        }
    }
    return false;
}

std::string normalizeCacheQuery(std::string_view query) {
    std::string normalized;
    normalized.reserve(query.size());

    bool previous_was_space = true;
    for (unsigned char ch : query) {
        if (std::isspace(ch) != 0) {
            if (!previous_was_space) {
                normalized.push_back(' ');
                previous_was_space = true;
            }
            continue;
        }

        normalized.push_back(static_cast<char>(std::tolower(ch)));
        previous_was_space = false;
    }

    if (!normalized.empty() && normalized.back() == ' ') {
        normalized.pop_back();
    }

    return normalized;
}

std::string normalizeSuggestionPrefix(std::string_view query) {
    std::string current_token;

    for (unsigned char ch : query) {
        if (std::isalnum(ch) != 0) {
            current_token += static_cast<char>(std::tolower(ch));
            continue;
        }

        current_token.clear();
    }

    return current_token;
}

std::string formatDuration(Clock::duration duration) {
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        duration).count();
    if (ms < 1000) {
        return std::to_string(ms) + " ms";
    }

    std::ostringstream out;
    out << std::fixed << std::setprecision(ms < 10000 ? 2 : 1)
        << (static_cast<double>(ms) / 1000.0) << " s";
    return out.str();
}

std::optional<uint64_t> readResidentMemoryKiB() {
    std::ifstream status("/proc/self/status");
    if (!status) {
        return std::nullopt;
    }

    std::string line;
    while (std::getline(status, line)) {
        if (line.rfind("VmRSS:", 0) != 0) {
            continue;
        }

        std::istringstream in(line.substr(6));
        uint64_t value = 0;
        std::string unit;
        if (in >> value >> unit) {
            return value;
        }
        return std::nullopt;
    }

    return std::nullopt;
}

void logMemoryUsage(const std::string& label) {
    auto memory_kib = readResidentMemoryKiB();
    if (!memory_kib) {
        std::cout << label << ": unavailable\n";
        return;
    }

    std::ostringstream out;
    out << std::fixed << std::setprecision(1)
        << (static_cast<double>(*memory_kib) / 1024.0);
    std::cout << label << ": " << out.str() << " MiB RSS\n";
}

std::optional<RuntimeConfig> parseRuntimeConfig(int argc, char* argv[]) {
    RuntimeConfig config;
    config.compute_pagerank_during_index =
        parseBoolEnv(std::getenv("WIKILENS_COMPUTE_PAGERANK"));

    if (const char* env_dump = std::getenv("WIKILENS_DUMP_PATH")) {
        if (*env_dump != '\0') {
            config.dump_path = env_dump;
            config.dump_path_overridden = true;
        }
    }

    if (const char* env_max_docs = std::getenv("WIKILENS_MAX_DOCS")) {
        auto parsed = parseSizeT(env_max_docs);
        if (!parsed) {
            std::cerr << "Invalid WIKILENS_MAX_DOCS value: "
                      << env_max_docs << "\n";
            return std::nullopt;
        }
        config.max_docs = *parsed;
    }

    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];

        if (arg == "--help") {
            printUsage(argv[0]);
            return std::nullopt;
        }

        if (arg == "--dump") {
            if (i + 1 >= argc) {
                std::cerr << "--dump requires a file path\n";
                printUsage(argv[0]);
                return std::nullopt;
            }

            config.dump_path = argv[++i];
            config.dump_path_overridden = true;
            continue;
        }

        if (arg == "--max-docs") {
            if (i + 1 >= argc) {
                std::cerr << "--max-docs requires a numeric value\n";
                printUsage(argv[0]);
                return std::nullopt;
            }

            auto parsed = parseSizeT(argv[++i]);
            if (!parsed) {
                std::cerr << "Invalid --max-docs value\n";
                printUsage(argv[0]);
                return std::nullopt;
            }

            config.max_docs = *parsed;
            continue;
        }

        if (arg == "--compute-pagerank") {
            config.compute_pagerank_during_index = true;
            continue;
        }

        if (arg == "--recompute-pagerank") {
            config.recompute_pagerank_only = true;
            continue;
        }

        if (arg == "--benchmark") {
            config.benchmark_only = true;
            continue;
        }

        if (arg == "--benchmark-queries") {
            if (i + 1 >= argc) {
                std::cerr << "--benchmark-queries requires a file path\n";
                printUsage(argv[0]);
                return std::nullopt;
            }

            config.benchmark_queries_path = argv[++i];
            continue;
        }

        if (arg == "--profile-indexing") {
            config.profile_indexing = true;
            continue;
        }

        std::cerr << "Unknown argument: " << arg << "\n";
        printUsage(argv[0]);
        return std::nullopt;
    }

    return config;
}

bool parseBoundedSizeParam(const char* raw_value,
                           const char* name,
                           size_t min_value,
                           size_t max_value,
                           size_t default_value,
                           size_t& output,
                           std::string& error) {
    output = default_value;
    if (!raw_value) {
        return true;
    }

    auto parsed = parseSizeT(raw_value);
    if (!parsed) {
        error = std::string("Invalid numeric value for '") + name + "'";
        return false;
    }

    if (*parsed < min_value || *parsed > max_value) {
        error = std::string("'") + name + "' must be between " +
                std::to_string(min_value) + " and " +
                std::to_string(max_value);
        return false;
    }

    output = *parsed;
    return true;
}

crow::response badRequest(const std::string& message) {
    crow::response res;
    res.code = 400;
    res.add_header("Access-Control-Allow-Origin", "*");
    res.body = message;
    return res;
}

crow::response jsonResponse(const crow::json::wvalue& body) {
    crow::response res;
    res.add_header("Access-Control-Allow-Origin", "*");
    res.write(body.dump());
    return res;
}

uint64_t fnv1a64(std::string_view value) {
    uint64_t hash = 1469598103934665603ULL;
    for (unsigned char ch : value) {
        hash ^= ch;
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::string artifactSuffix(const RuntimeConfig& config) {
    if (!config.dump_path_overridden && config.max_docs == 0) {
        return {};
    }

    std::ostringstream raw;
    raw << std::filesystem::weakly_canonical(config.dump_path).string()
        << "|" << config.max_docs;

    std::ostringstream out;
    out << std::hex << fnv1a64(raw.str());
    return "_" + out.str();
}

ArtifactPaths buildArtifactPaths(const RuntimeConfig& config) {
    ArtifactPaths paths;
    paths.cache_dir = "../data/cache";

    const std::string suffix = artifactSuffix(config);
    if (suffix.empty()) {
        paths.index_cache = paths.cache_dir / "inverted_index.bin";
        paths.store_cache = paths.cache_dir / "document_store.bin";
        paths.link_graph = paths.cache_dir / "link_graph.txt";
    } else {
        paths.index_cache = paths.cache_dir / ("inverted_index" + suffix + ".bin");
        paths.store_cache = paths.cache_dir / ("document_store" + suffix + ".bin");
        paths.link_graph = paths.cache_dir / ("link_graph" + suffix + ".txt");
    }

    return paths;
}

// Encode a Wikipedia article title into a canonical Wikipedia URL.
// Spaces become underscores; characters that would break a URL are
// percent-encoded; non-ASCII UTF-8 bytes pass through unchanged.
std::string wikiUrlEncode(const std::string& title) {
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

std::string wikiUrl(const std::string& title) {
    return "https://en.wikipedia.org/wiki/" + wikiUrlEncode(title);
}

std::string snippetFrom(const std::string& text,
                        size_t max_chars = 200) {
    if (text.size() <= max_chars) return text;
    size_t cut = text.rfind(' ', max_chars);
    if (cut == std::string::npos) cut = max_chars;
    std::string out = text.substr(0, cut);
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out + "\xe2\x80\xa6";
}

bool saveLinkGraph(const std::filesystem::path& path,
                   const std::vector<std::string>& titles_by_id,
                   const std::vector<std::vector<std::string>>& raw_links) {
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        return false;
    }

    const size_t count = std::min(titles_by_id.size(), raw_links.size());
    for (size_t src = 0; src < count; ++src) {
        for (const std::string& target : raw_links[src]) {
            out << titles_by_id[src] << '\t' << target << '\n';
        }
    }

    return static_cast<bool>(out);
}

std::vector<std::vector<uint32_t>> buildAdjacencyList(
    const std::unordered_map<std::string, uint32_t>& title_to_id,
    const std::vector<std::vector<std::string>>& raw_links,
    size_t doc_count) {
    std::vector<std::vector<uint32_t>> adjacency_list(doc_count);
    const size_t count = std::min(raw_links.size(), doc_count);

    for (size_t src = 0; src < count; ++src) {
        for (const std::string& target : raw_links[src]) {
            auto it = title_to_id.find(target);
            if (it != title_to_id.end()) {
                adjacency_list[src].push_back(it->second);
            }
        }
    }

    return adjacency_list;
}

bool loadAdjacencyListFromEdgeFile(const std::filesystem::path& path,
                                   const DocumentStore& store,
                                   std::vector<std::vector<uint32_t>>& adjacency_list) {
    std::ifstream in(path);
    if (!in) {
        return false;
    }

    adjacency_list.assign(store.totalDocs(), {});

    std::unordered_map<std::string, uint32_t> title_to_id;
    title_to_id.reserve(store.totalDocs());

    for (const auto& [doc_id, doc] : store.allDocuments()) {
        title_to_id[doc.title] = doc_id;
    }

    std::string line;
    while (std::getline(in, line)) {
        const size_t tab = line.find('\t');
        if (tab == std::string::npos) {
            continue;
        }

        std::string source = line.substr(0, tab);
        std::string target = line.substr(tab + 1);

        auto src_it = title_to_id.find(source);
        auto dst_it = title_to_id.find(target);
        if (src_it == title_to_id.end() ||
            dst_it == title_to_id.end()) {
            continue;
        }

        adjacency_list[src_it->second].push_back(dst_it->second);
    }

    return true;
}

void applyPageRankScores(DocumentStore& store,
                         const std::vector<std::vector<uint32_t>>& adjacency_list) {
    if (adjacency_list.empty()) {
        std::cout << "Skipping PageRank because no documents were indexed.\n";
        return;
    }

    std::cout << "Computing PageRank...\n";

    PageRank pr(0.85, 30);
    std::vector<double> pr_scores = pr.compute(adjacency_list);
    if (pr_scores.empty()) {
        return;
    }

    double max_pr = *std::max_element(pr_scores.begin(),
                                      pr_scores.end());

    for (auto& score : pr_scores) {
        if (max_pr > 0.0) {
            score /= max_pr;
            score = std::log(1.0 + score);
        } else {
            score = 0.0;
        }
    }

    for (uint32_t i = 0; i < pr_scores.size(); ++i) {
        store.setPageRank(i, pr_scores[i]);
    }
}

bool recomputePageRankFromArtifacts(const ArtifactPaths& artifacts) {
    DocumentStore store;
    if (!store.loadFromFile(artifacts.store_cache.string())) {
        std::cerr << "Could not load document store cache: "
                  << artifacts.store_cache << "\n";
        return false;
    }

    std::vector<std::vector<uint32_t>> adjacency_list;
    if (!loadAdjacencyListFromEdgeFile(artifacts.link_graph,
                                       store,
                                       adjacency_list)) {
        std::cerr << "Could not load link graph edge list: "
                  << artifacts.link_graph << "\n";
        return false;
    }

    const auto started = Clock::now();
    applyPageRankScores(store, adjacency_list);
    std::cout << "Offline PageRank finished in "
              << formatDuration(Clock::now() - started) << "\n";

    if (!store.saveToFile(artifacts.store_cache.string())) {
        std::cerr << "Failed to save updated document store cache: "
                  << artifacts.store_cache << "\n";
        return false;
    }

    std::cout << "Updated document store with offline PageRank scores.\n";
    return true;
}

std::vector<std::string> defaultBenchmarkQueries() {
    return {
        "computer",
        "science",
        "history",
        "new york",
        "world war",
        "alan turing",
        "music",
        "football",
        "river",
        "language",
        "mathematics",
        "internet"
    };
}

std::vector<std::string> loadBenchmarkQueries(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) {
        return {};
    }

    std::vector<std::string> queries;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        if (hasNonWhitespace(line)) {
            queries.push_back(line);
        }
    }

    return queries;
}

double percentileMs(std::vector<double> sorted_values, double percentile) {
    if (sorted_values.empty()) {
        return 0.0;
    }

    std::sort(sorted_values.begin(), sorted_values.end());
    const double rank = percentile * static_cast<double>(sorted_values.size() - 1);
    const size_t lower = static_cast<size_t>(std::floor(rank));
    const size_t upper = static_cast<size_t>(std::ceil(rank));
    if (lower == upper) {
        return sorted_values[lower];
    }

    const double weight = rank - static_cast<double>(lower);
    return sorted_values[lower] * (1.0 - weight) +
           sorted_values[upper] * weight;
}

std::vector<SearchResult> executeSearch(QueryEngine& engine,
                                        const SearchCacheKey& key) {
    return engine.search(key.query, key.limit, key.offset);
}

std::vector<SearchResult> executeCachedSearch(QueryEngine& engine,
                                              LRUCache<SearchCacheKey,
                                                       std::vector<SearchResult>,
                                                       SearchCacheKeyHash>& cache,
                                              SearchCacheStats& stats,
                                              const SearchCacheKey& key) {
    if (auto cached = cache.get(key)) {
        ++stats.hits;
        return *cached;
    }

    ++stats.misses;
    auto results = executeSearch(engine, key);
    cache.put(key, results);
    return results;
}

std::vector<SearchResult> executeSharedCachedSearch(
    QueryEngine& engine,
    LRUCache<SearchCacheKey, std::vector<SearchResult>, SearchCacheKeyHash>& cache,
    SearchCacheStats& stats,
    std::mutex& cache_mutex,
    std::mutex& engine_mutex,
    const SearchCacheKey& key) {

    {
        std::scoped_lock lock(cache_mutex);
        if (auto cached = cache.get(key)) {
            ++stats.hits;
            return *cached;
        }
        ++stats.misses;
    }

    std::vector<SearchResult> results;
    {
        std::scoped_lock lock(engine_mutex);
        results = executeSearch(engine, key);
    }

    {
        std::scoped_lock lock(cache_mutex);
        cache.put(key, results);
    }

    return results;
}

void printBenchmarkSummary(const std::string& label,
                           const std::vector<double>& latencies_ms) {
    if (latencies_ms.empty()) {
        std::cout << label << ": no samples\n";
        return;
    }

    std::vector<double> sorted = latencies_ms;
    std::sort(sorted.begin(), sorted.end());

    double total = 0.0;
    for (double sample : sorted) {
        total += sample;
    }

    std::cout << label << ":\n";
    std::cout << "  Samples: " << sorted.size() << "\n";
    std::cout << std::fixed << std::setprecision(3)
              << "  Avg: " << (total / static_cast<double>(sorted.size())) << " ms\n"
              << "  P50: " << percentileMs(sorted, 0.50) << " ms\n"
              << "  P95: " << percentileMs(sorted, 0.95) << " ms\n"
              << "  Max: " << sorted.back() << " ms\n";
}

void runBenchmark(QueryEngine& engine,
                  size_t total_docs,
                  const RuntimeConfig& config) {
    std::vector<std::string> queries = config.benchmark_queries_path.empty()
        ? defaultBenchmarkQueries()
        : loadBenchmarkQueries(config.benchmark_queries_path);

    if (queries.empty()) {
        std::cerr << "No benchmark queries available.\n";
        return;
    }

    std::cout << "Running query benchmark against "
              << total_docs << " indexed documents.\n";
    std::cout << "Benchmark queries: " << queries.size() << "\n";
    if (!config.benchmark_queries_path.empty()) {
        std::cout << "Query file: " << config.benchmark_queries_path << "\n";
    }

    std::vector<double> uncached_latencies;
    uncached_latencies.reserve(queries.size());

    for (const std::string& query : queries) {
        const auto started = Clock::now();
        (void)executeSearch(engine, {normalizeCacheQuery(query), kDefaultLimit, 0});
        const auto elapsed = std::chrono::duration_cast<Nanoseconds>(
            Clock::now() - started);
        uncached_latencies.push_back(
            static_cast<double>(elapsed.count()) / 1'000'000.0);
    }

    LRUCache<SearchCacheKey, std::vector<SearchResult>, SearchCacheKeyHash> cache(
        kSearchCacheCapacity);
    SearchCacheStats cache_stats;

    for (const std::string& query : queries) {
        (void)executeCachedSearch(engine, cache, cache_stats,
                                  {normalizeCacheQuery(query), kDefaultLimit, 0});
    }

    std::vector<double> cached_latencies;
    cached_latencies.reserve(queries.size());

    for (const std::string& query : queries) {
        const auto started = Clock::now();
        (void)executeCachedSearch(engine, cache, cache_stats,
                                  {normalizeCacheQuery(query), kDefaultLimit, 0});
        const auto elapsed = std::chrono::duration_cast<Nanoseconds>(
            Clock::now() - started);
        cached_latencies.push_back(
            static_cast<double>(elapsed.count()) / 1'000'000.0);
    }

    printBenchmarkSummary("Uncached search latency", uncached_latencies);
    printBenchmarkSummary("Warm-cache search latency", cached_latencies);
    std::cout << "Cache warmup+benchmark hits: " << cache_stats.hits
              << ", misses: " << cache_stats.misses << "\n";
}

crow::json::wvalue buildSearchResponse(const DocumentStore& store,
                                       const std::vector<SearchResult>& results) {
    crow::json::wvalue response;
    response["results"] = crow::json::wvalue::list();

    for (size_t i = 0; i < results.size(); ++i) {
        const Document* doc = store.getDocument(results[i].doc_id);
        if (!doc) continue;

        crow::json::wvalue item;
        item["title"] = doc->title;
        item["url"] = doc->url;
        item["snippet"] = doc->snippet;
        item["score"] = results[i].score;
        item["pagerank"] = doc->pagerank;

        response["results"][i] = std::move(item);
    }

    return response;
}

} // namespace

int main(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--help") {
            printUsage(argv[0]);
            return 0;
        }
    }

    auto config = parseRuntimeConfig(argc, argv);
    if (!config) {
        return 1;
    }

    const ArtifactPaths artifacts = buildArtifactPaths(*config);
    std::filesystem::create_directories(artifacts.cache_dir);

    if (config->recompute_pagerank_only) {
        return recomputePageRankFromArtifacts(artifacts) ? 0 : 1;
    }

    std::cout << "Starting WikiLens ingestion...\n";
    std::cout << "Dump path: " << config->dump_path << "\n";
    std::cout << "Index cache: " << artifacts.index_cache << "\n";
    std::cout << "Store cache: " << artifacts.store_cache << "\n";
    std::cout << "Link graph: " << artifacts.link_graph << "\n";

    if (config->max_docs > 0) {
        std::cout << "Indexing limit: "
                  << config->max_docs
                  << " documents\n";
    } else {
        std::cout << "Indexing limit: none\n";
    }

    std::cout << "Compute PageRank during indexing: "
              << (config->compute_pagerank_during_index ? "yes" : "no")
              << "\n";

    Tokenizer tokenizer("../data/stopwords.txt");
    WikitextStripper stripper;
    InvertedIndex index;
    DocumentStore store;
    IndexingProfile profile;
    profile.enabled = config->profile_indexing;

    bool loaded_from_cache = false;
    if (std::filesystem::exists(artifacts.index_cache) &&
        std::filesystem::exists(artifacts.store_cache)) {

        std::cout << "Found cache files. Loading index...\n";
        const auto cache_load_started = Clock::now();

        bool index_ok = index.loadFromFile(artifacts.index_cache.string());
        bool store_ok = store.loadFromFile(artifacts.store_cache.string());

        std::cout << "Cache load finished in "
                  << formatDuration(Clock::now() - cache_load_started)
                  << "\n";

        if (index_ok && store_ok) {
            loaded_from_cache = true;
            std::cout << "Loaded cached index and document store.\n";
            logMemoryUsage("Memory after cache load");
        } else {
            std::cout << "Cache load failed. Rebuilding from dump...\n";
        }
    }

    if (!loaded_from_cache) {
        const auto indexing_started = Clock::now();

        std::unordered_map<std::string, uint32_t> title_to_id;
        std::vector<std::string> titles_by_id;
        std::vector<std::vector<std::string>> raw_links;

        uint32_t doc_id = 0;
        WikiParser parser;

        bool parsed_ok = parser.parse(
            config->dump_path.string(),
            [&](const std::string& title,
                const std::string& text) {

                const auto strip_started = Clock::now();
                StripResult result = stripper.clean(text);
                profile.strip_time += std::chrono::duration_cast<Nanoseconds>(
                    Clock::now() - strip_started);

                const auto tokenize_started = Clock::now();
                auto tokens = tokenizer.tokenize(result.cleaned_text);
                profile.tokenize_time += std::chrono::duration_cast<Nanoseconds>(
                    Clock::now() - tokenize_started);

                const auto index_started = Clock::now();
                index.addDocument(doc_id, tokens);
                store.addDocument(doc_id, title, wikiUrl(title),
                                  snippetFrom(result.cleaned_text));

                title_to_id[title] = doc_id;
                titles_by_id.push_back(title);
                raw_links.push_back(result.internal_links);
                profile.index_time += std::chrono::duration_cast<Nanoseconds>(
                    Clock::now() - index_started);
                ++profile.docs_processed;

                ++doc_id;

                if (doc_id % 1000 == 0) {
                    std::cout << "Indexed "
                              << doc_id
                              << " pages\n";
                }

                if (config->max_docs > 0 && doc_id >= config->max_docs) {
                    std::cout << "Stopping after requested limit of "
                              << config->max_docs
                              << " pages.\n";
                    return false;
                }

                return true;
            }
        );

        if (!parsed_ok) {
            return 1;
        }

        if (saveLinkGraph(artifacts.link_graph, titles_by_id, raw_links)) {
            std::cout << "Saved link graph edge list to "
                      << artifacts.link_graph << "\n";
        } else {
            std::cerr << "Warning: failed to save link graph edge list to "
                      << artifacts.link_graph << "\n";
        }

        if (config->compute_pagerank_during_index) {
            auto adjacency_list = buildAdjacencyList(title_to_id,
                                                     raw_links,
                                                     titles_by_id.size());
            applyPageRankScores(store, adjacency_list);
        } else {
            std::cout << "Skipping PageRank during indexing.\n";
            std::cout << "Run `" << argv[0]
                      << " --recompute-pagerank";
            if (config->dump_path_overridden) {
                std::cout << " --dump \"" << config->dump_path.string() << "\"";
            }
            if (config->max_docs > 0) {
                std::cout << " --max-docs " << config->max_docs;
            }
            std::cout << "` to compute it offline from the saved edge list.\n";
        }

        bool saved_index = index.saveToFile(artifacts.index_cache.string());
        bool saved_store = store.saveToFile(artifacts.store_cache.string());

        if (saved_index && saved_store) {
            std::cout << "Saved cache artifacts.\n";
        } else {
            std::cout << "Warning: failed to save cache files.\n";
        }

        std::cout << "Indexing finished in "
                  << formatDuration(Clock::now() - indexing_started)
                  << "\n";
        logMemoryUsage("Memory after indexing");
        profile.print();
    }

    std::cout << "Ingestion complete.\n";
    std::cout << "Total documents: "
              << index.totalDocs() << "\n";

    QueryEngine engine(index, tokenizer, store);

    if (config->benchmark_only) {
        runBenchmark(engine, index.totalDocs(), *config);
        return 0;
    }

    LRUCache<SearchCacheKey, std::vector<SearchResult>, SearchCacheKeyHash> search_cache(
        kSearchCacheCapacity);
    SearchCacheStats search_cache_stats;
    std::mutex search_cache_mutex;
    std::mutex engine_mutex;

    crow::SimpleApp app;

    CROW_ROUTE(app, "/")
    ([]() {
        return "WikiLens Search Engine Running";
    });

    CROW_ROUTE(app, "/stats")
    ([&]() {
        size_t cache_size = 0;
        uint64_t cache_hits = 0;
        uint64_t cache_misses = 0;
        {
            std::scoped_lock lock(search_cache_mutex);
            cache_size = search_cache.size();
            cache_hits = search_cache_stats.hits;
            cache_misses = search_cache_stats.misses;
        }

        crow::json::wvalue response;
        response["total_docs"] = index.totalDocs();
        response["avg_doc_length"] = index.avgDocLength();
        response["term_count"] = static_cast<uint64_t>(index.termCount());
        response["query_cache_size"] = static_cast<uint64_t>(cache_size);
        response["query_cache_capacity"] = static_cast<uint64_t>(search_cache.capacity());
        response["query_cache_hits"] = cache_hits;
        response["query_cache_misses"] = cache_misses;
        return jsonResponse(response);
    });

    CROW_ROUTE(app, "/suggest")
    ([&](const crow::request& req) {
        auto query = req.url_params.get("q");
        if (!query) {
            return badRequest("Missing query parameter 'q'");
        }

        std::string_view query_view(query);
        if (!hasNonWhitespace(query_view)) {
            return badRequest("Query parameter 'q' must not be empty");
        }

        if (query_view.size() > kMaxQueryLength) {
            return badRequest("Query parameter 'q' exceeds the 512 character limit");
        }

        size_t limit = kDefaultSuggestLimit;
        std::string parse_error;
        if (!parseBoundedSizeParam(req.url_params.get("limit"),
                                   "limit",
                                   1,
                                   kMaxSuggestLimit,
                                   kDefaultSuggestLimit,
                                   limit,
                                   parse_error)) {
            return badRequest(parse_error);
        }

        std::string prefix = normalizeSuggestionPrefix(query_view);

        crow::json::wvalue response;
        response["suggestions"] = crow::json::wvalue::list();

        if (prefix.empty()) {
            return jsonResponse(response);
        }

        auto suggestions = index.suggestPrefix(prefix, limit);
        for (size_t i = 0; i < suggestions.size(); ++i) {
            response["suggestions"][i] = suggestions[i];
        }

        return jsonResponse(response);
    });

    CROW_ROUTE(app, "/search")
    ([&](const crow::request& req) {
        auto query = req.url_params.get("q");
        if (!query) {
            return badRequest("Missing query parameter 'q'");
        }

        std::string_view query_view(query);
        if (!hasNonWhitespace(query_view)) {
            return badRequest("Query parameter 'q' must not be empty");
        }

        if (query_view.size() > kMaxQueryLength) {
            return badRequest("Query parameter 'q' exceeds the 512 character limit");
        }

        size_t limit = kDefaultLimit;
        size_t offset = 0;
        std::string parse_error;

        if (!parseBoundedSizeParam(req.url_params.get("limit"),
                                   "limit",
                                   1,
                                   kMaxLimit,
                                   kDefaultLimit,
                                   limit,
                                   parse_error)) {
            return badRequest(parse_error);
        }

        if (!parseBoundedSizeParam(req.url_params.get("offset"),
                                   "offset",
                                   0,
                                   std::numeric_limits<size_t>::max(),
                                   0,
                                   offset,
                                   parse_error)) {
            return badRequest(parse_error);
        }

        SearchCacheKey key{normalizeCacheQuery(query_view), limit, offset};
        auto results = executeSharedCachedSearch(engine,
                                                search_cache,
                                                search_cache_stats,
                                                search_cache_mutex,
                                                engine_mutex,
                                                key);

        return jsonResponse(buildSearchResponse(store, results));
    });

    std::cout << "Starting HTTP server on port 8080...\n";

    app.port(8080).multithreaded().run();

    return 0;
}
