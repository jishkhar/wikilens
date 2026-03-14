# 🔍 Building a Search Engine from Scratch in C++
### Dataset: Wikipedia Dump

---

## Table of Contents
1. [High-Level Architecture](#1-high-level-architecture)
2. [Wikipedia as the Dataset](#2-wikipedia-as-the-dataset)
3. [Component Breakdown](#3-component-breakdown)
4. [Data Structures & Algorithms](#4-data-structures--algorithms)
5. [Implementation Plan (Phase by Phase)](#5-implementation-plan-phase-by-phase)
6. [C++ Code Skeletons](#6-c-code-skeletons)
7. [Scalability & Distributed Design](#7-scalability--distributed-design)
8. [Amazon Interview Tips](#8-amazon-interview-tips)
9. [Project File Structure](#9-project-file-structure)
10. [Libraries & Tools](#10-libraries--tools)

---

## 1. High-Level Architecture

```
┌──────────────────────────────────────────────────────────┐
│                        USER QUERY                        │
└─────────────────────────┬────────────────────────────────┘
                          │
                          ▼
┌──────────────────────────────────────────────────────────┐
│                   QUERY PROCESSOR                        │
│        (Tokenize → Normalize → Parse → Expand)           │
└─────────────────────────┬────────────────────────────────┘
                          │
                          ▼
┌──────────────────────────────────────────────────────────┐
│                    RANKING ENGINE                        │
│              (BM25 / TF-IDF / PageRank)                  │
└─────────────────────────┬────────────────────────────────┘
                          │
          ┌───────────────┴──────────────┐
          ▼                              ▼
┌──────────────────┐          ┌──────────────────────┐
│  INVERTED INDEX  │          │    DOCUMENT STORE    │
│  (term → doc)   │          │  (docID → metadata)  │
└──────────────────┘          └──────────────────────┘
          ▲                              ▲
          └───────────────┬─────────────┘
                          │
┌─────────────────────────┴────────────────────────────────┐
│                      INDEXER                             │
│          (Parse → Tokenize → Build Index)                │
└─────────────────────────┬────────────────────────────────┘
                          │
                          ▼
┌──────────────────────────────────────────────────────────┐
│                    WEB CRAWLER                           │
│         (Fetch → Parse HTML → URL Frontier)              │
└──────────────────────────────────────────────────────────┘
```

---

## 2. Wikipedia as the Dataset

### Why Wikipedia
Instead of building a full web crawler, we use the official Wikipedia data dumps as our corpus. This is a deliberate and mature engineering decision — it lets us skip the crawler complexity and focus on the hard problems: indexing at scale and ranking quality. This is a credible, well-respected approach used in academic search engine research.

### The Data
Wikipedia publishes official dumps at `dumps.wikimedia.org`. The main file we care about is:

```
enwiki-latest-pages-articles.xml.bz2
```

| Dump | Compressed Size | Articles | Good For |
|------|----------------|----------|---------|
| Simple English Wiki | ~250 MB | ~220K | Development & testing |
| Full English Wiki | ~22 GB | ~6.7M | Production-scale indexing |

**Recommended approach:** start with Simple English for building and validating the pipeline, then point it at the full English dump once everything works.

### What the Dump Looks Like
The dump is a large XML file. Each article looks roughly like this:

```xml
<page>
  <title>Alan Turing</title>
  <id>1089</id>
  <revision>
    <text>
      Alan Mathison Turing was an English mathematician...
      == Early life ==
      Turing was born on 23 June 1912...
      [[Cryptography]] and [[Computer science]] are...
    </text>
  </revision>
</page>
```

The body text is in **Wikitext** format — not plain HTML — so it needs its own cleanup step.

### Wikipedia Ingestion Pipeline

The ingestion pipeline replaces the web crawler entirely. It is a sequential pipeline with four stages:

**Stage 1 — Streaming XML Parser**
The dump is too large to load into memory at once (~90GB uncompressed for full English). We use a SAX-style streaming XML parser that reads the file tag by tag, extracting `<title>` and `<text>` for each `<page>` without ever holding the whole file in RAM. We decompress `bz2` on the fly using `libbz2` so we never need to store the uncompressed dump on disk.

**Stage 2 — Wikitext Markup Stripper**
Raw Wikitext is full of syntax that needs cleaning before indexing. The stripper handles:
- Remove templates: `{{Infobox person | ...}}` — metadata blocks, not readable text
- Convert internal links: `[[Alan Turing|Turing]]` → extract display text, record link target for PageRank
- Remove external links: `[http://example.com Example]` → extract `Example`
- Remove heading markers: `== Early life ==` → keep `Early life`
- Remove bold/italic markers: `'''bold'''` and `''italic''`
- Remove reference tags: `<ref>...</ref>`
- Remove HTML comments: `<!-- ... -->`
- Remove tables: `{| ... |}` (complex, can skip for v1)

After stripping, we have clean plain text ready for the tokenizer.

**Stage 3 — Link Graph Extraction**
While stripping Wikitext, we also extract every internal link (`[[Target Article]]`) and record the edge `(source_article → target_article)`. This link graph is saved separately and used later for offline PageRank computation. Wikipedia's internal link structure is a great proxy for authority — heavily linked-to articles are generally more important.

**Stage 4 — Feed to Indexer**
Each cleaned article is passed to the indexer as a document with:
- `doc_id` — a monotonically incrementing integer
- `title` — the article title
- `url` — constructed as `https://en.wikipedia.org/wiki/{title}` (URL-encoded)
- `body` — clean plain text after markup stripping
- `length` — word count, stored for BM25 normalization

### Development Workflow with Wikipedia

```
Download dump (bz2)
       │
       ▼
Stream & parse XML  ──────────────────────────┐
       │                                      │
       ▼                                      ▼
Strip Wikitext markup                 Extract link graph
       │                                      │
       ▼                                      ▼
Feed to Indexer                     Save as edge list file
       │                                      │
       ▼                                      ▼
  Inverted Index                    Offline PageRank computation
       │                                      │
       └──────────────────┬───────────────────┘
                          ▼
                    Query Engine
```

### Scaling Notes
- **Single-threaded ingestion is fine for v1** — parsing and indexing ~6.7M articles takes a few hours on one thread, acceptable for an offline build process.
- **For v2**, the dump can be split into chunks and indexed in parallel across multiple threads, with index segments merged at the end (similar to how Lucene builds segment files).
- **Index rebuild frequency** — Wikipedia dumps are published monthly. For a real system you'd rebuild or apply delta updates monthly.

### What to Tell the Interviewer
*"I replaced the crawler with the Wikipedia dump for two reasons: first, it gave me a clean, well-understood corpus to validate ranking quality; second, it let me focus engineering effort on indexing and retrieval rather than crawler infrastructure. The ingestion pipeline — streaming XML parsing, Wikitext stripping, link graph extraction — is functionally equivalent to what a crawler's document processor would do, just reading from a local file instead of HTTP responses."*

---

## 3. Component Breakdown

### 3.1 Wikipedia Ingestion (replaces Web Crawler)
Since we're using the Wikipedia dump, the crawler is replaced by the ingestion pipeline described in Section 2. For interview purposes, you should still be able to explain how a crawler would work at a high level — the ingestion pipeline covers the same logical responsibilities.

**Crawler concepts to know for the interview (even if not building one):**
- URL frontier — a priority queue of URLs to visit, with politeness throttling per domain
- Duplicate detection — hashing URLs and content to avoid re-indexing the same page
- `robots.txt` parsing — respecting crawl rules per domain
- Distributed crawling — partitioning the URL space across multiple crawler nodes
- BFS vs priority-based crawling — prioritizing high-PageRank or freshly updated pages

---

### 3.2 HTML Parser & Text Extractor
Takes raw HTML and extracts clean text, title, metadata, and outgoing links.

**Responsibilities:**
- Strip HTML tags
- Extract `<title>`, `<meta>`, `<h1-h6>`, `<body>` content
- Identify and extract all `<a href>` links
- Handle encoding (UTF-8, ISO-8859-1, etc.)

---

### 3.3 Indexer
The core of the search engine. Converts documents into a searchable inverted index.

**Responsibilities:**
- Tokenization — split text into terms
- Normalization — lowercase, remove punctuation
- Stemming — "running" → "run" (use Porter Stemmer)
- Stop word removal — remove "the", "a", "is", etc.
- Build and persist the **Inverted Index**
- Store term frequency, document frequency, and positions

---

### 3.4 Inverted Index
The central data structure. Maps each term to the list of documents containing it.

```
"algorithm" → [(doc_1, tf=3, positions=[10,45,78]), (doc_5, tf=1, positions=[22])]
"amazon"    → [(doc_2, tf=5, positions=[1,3,7,8,9]), (doc_3, tf=2, positions=[14,30])]
```

**Storage format:**
- In-memory: `unordered_map<string, vector<PostingEntry>>`
- On-disk: sorted term dictionary + posting lists (similar to SSTable)

---

### 3.5 Query Processor
Takes a raw user query and transforms it into an internal query representation.

**Responsibilities:**
- Tokenize and normalize the query (same pipeline as indexer)
- Parse query operators (`AND`, `OR`, `NOT`, phrase queries `"exact phrase"`)
- Query expansion (synonyms, spelling correction — mention as optional)
- Look up posting lists for each query term

---

### 3.6 Ranking Engine
Scores documents against the query and returns a ranked list.

**Algorithms to know:**

| Algorithm | Complexity | Best For |
|-----------|-----------|---------|
| TF-IDF    | Simple    | Baseline relevance |
| BM25      | Moderate  | State-of-the-art term-based |
| PageRank  | Complex   | Link-based authority |
| Combined  | Complex   | Production systems |

**BM25 Formula (must know this for interview):**
```
Score(D, Q) = Σ IDF(qᵢ) · (tf(qᵢ,D) · (k1+1)) / (tf(qᵢ,D) + k1·(1-b+b·|D|/avgDL))

Where:
  k1 = 1.2–2.0 (term saturation)
  b  = 0.75    (length normalization)
  |D| = document length
  avgDL = average document length in corpus
```

---

### 3.7 Document Store
A key-value store mapping document IDs to metadata.

**Stores:**
- URL
- Title
- Snippet / excerpt
- Crawl timestamp
- Page rank score
- Document length

---

## 4. Data Structures & Algorithms

### Core Data Structures

| Structure | Used For | C++ Type |
|-----------|---------|----------|
| Inverted Index | term → posting list | `unordered_map<string, vector<Posting>>` |
| URL Frontier | BFS crawl queue | `std::queue` + `std::unordered_set` for visited |
| Document Store | docID → metadata | `unordered_map<uint64_t, Document>` |
| Priority Queue | Ranking top-K results | `std::priority_queue` |
| Bloom Filter | Fast duplicate URL detection | Custom bitset |
| Skip Lists | Fast posting list intersection | Custom implementation |
| Trie | Query autocomplete | Custom trie |

### Key Algorithms

**Posting List Intersection (AND queries):**
```
Two-pointer merge on sorted posting lists — O(n + m)
```

**Union (OR queries):**
```
Priority queue merge of multiple posting lists — O(N log k)
```

**Top-K Results:**
```
Min-heap of size K — O(N log K)
```

---

## 5. Implementation Plan (Phase by Phase)

### Phase 1 — Foundation (Week 1–2)
- [ ] Set up CMake project structure
- [ ] Implement tokenizer (whitespace, punctuation splitting)
- [ ] Implement normalizer (lowercase, strip punctuation)
- [ ] Implement simple Porter Stemmer
- [ ] Load stop words from file
- [ ] Unit test all text processing components

### Phase 2 — Indexer (Week 3–4)
- [ ] Design `Posting` and `PostingList` structs
- [ ] Build in-memory inverted index from a document corpus
- [ ] Compute TF and DF for all terms
- [ ] Serialize/deserialize index to disk (binary format)
- [ ] Build document store (docID → metadata)

### Phase 3 — Query Engine (Week 5)
- [ ] Query tokenizer and normalizer (reuse Phase 1)
- [ ] Single-term query lookup
- [ ] Multi-term AND/OR query with posting list merge
- [ ] Phrase query support (use positional index)
- [ ] BM25 scoring implementation

### Phase 4 — Wikipedia Ingestion (Week 6–7)
- [ ] Download Simple English Wikipedia dump from `dumps.wikimedia.org` for dev
- [ ] Implement SAX-style streaming XML parser using `libexpat` (no full-file load into RAM)
- [ ] Implement `bz2` decompression on the fly using `libbz2`
- [ ] Implement Wikitext markup stripper (templates, links, refs, headings, tables)
- [ ] Extract internal link graph while stripping — save as edge list file
- [ ] Wire up: XML stream → stripper → tokenizer → indexer pipeline
- [ ] Test pipeline end-to-end on Simple English dump (~220K articles)
- [ ] Swap in full English dump and run at scale

### Phase 5 — Ranking & Results (Week 8)
- [ ] BM25 scorer integrated with query engine
- [ ] PageRank computation on link graph (offline)
- [ ] Combine BM25 + PageRank into final score
- [ ] Top-K results with snippet extraction
- [ ] Simple CLI or HTTP API for querying

### Phase 6 — Scale & Polish (Week 9–10)
- [ ] Index partitioning (by term range or document range)
- [ ] Caching frequent queries (LRU cache)
- [ ] Query latency benchmarking
- [ ] Stress test with large corpus
- [ ] Write design doc

---

## 6. C++ Code Skeletons

### 5.1 Core Data Structures

```cpp
// posting.h
#pragma once
#include <cstdint>
#include <vector>

struct Posting {
    uint32_t doc_id;
    uint16_t term_freq;
    std::vector<uint32_t> positions; // for phrase queries
};

struct PostingList {
    uint32_t doc_freq;              // number of docs containing this term
    std::vector<Posting> postings;  // sorted by doc_id
};
```

### 5.2 Tokenizer

```cpp
// tokenizer.h
#pragma once
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <cctype>

class Tokenizer {
public:
    std::vector<std::string> tokenize(const std::string& text) {
        std::vector<std::string> tokens;
        std::string token;

        for (char c : text) {
            if (std::isalnum(c)) {
                token += std::tolower(c);
            } else if (!token.empty()) {
                if (!isStopWord(token)) {
                    tokens.push_back(stem(token));
                }
                token.clear();
            }
        }
        if (!token.empty() && !isStopWord(token)) {
            tokens.push_back(stem(token));
        }
        return tokens;
    }

private:
    bool isStopWord(const std::string& word);
    std::string stem(const std::string& word); // Porter Stemmer
};
```

### 5.3 Inverted Index

```cpp
// inverted_index.h
#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include "posting.h"

class InvertedIndex {
public:
    void addDocument(uint32_t doc_id, const std::vector<std::string>& tokens) {
        for (uint32_t pos = 0; pos < tokens.size(); ++pos) {
            const std::string& term = tokens[pos];
            auto& plist = index_[term];
            
            // find or create posting for this doc
            if (plist.postings.empty() || plist.postings.back().doc_id != doc_id) {
                plist.postings.push_back({doc_id, 1, {pos}});
                plist.doc_freq++;
            } else {
                plist.postings.back().term_freq++;
                plist.postings.back().positions.push_back(pos);
            }
        }
        total_docs_++;
        doc_lengths_[doc_id] = tokens.size();
    }

    const PostingList* lookup(const std::string& term) const {
        auto it = index_.find(term);
        return (it != index_.end()) ? &it->second : nullptr;
    }

    uint32_t totalDocs() const { return total_docs_; }
    uint32_t docLength(uint32_t doc_id) const {
        auto it = doc_lengths_.find(doc_id);
        return it != doc_lengths_.end() ? it->second : 0;
    }
    double avgDocLength() const;

    void saveToDisk(const std::string& path) const;
    void loadFromDisk(const std::string& path);

private:
    std::unordered_map<std::string, PostingList> index_;
    std::unordered_map<uint32_t, uint32_t> doc_lengths_;
    uint32_t total_docs_ = 0;
};
```

### 5.4 BM25 Scorer

```cpp
// bm25.h
#pragma once
#include <cmath>
#include <string>
#include <vector>
#include <unordered_map>
#include "inverted_index.h"

class BM25Scorer {
    static constexpr double k1 = 1.5;
    static constexpr double b  = 0.75;

public:
    BM25Scorer(const InvertedIndex& index) : index_(index) {}

    std::unordered_map<uint32_t, double> score(
        const std::vector<std::string>& query_terms
    ) {
        std::unordered_map<uint32_t, double> scores;
        double avgDL = index_.avgDocLength();
        uint32_t N   = index_.totalDocs();

        for (const auto& term : query_terms) {
            const PostingList* plist = index_.lookup(term);
            if (!plist) continue;

            double df  = plist->doc_freq;
            double idf = std::log((N - df + 0.5) / (df + 0.5) + 1.0);

            for (const auto& posting : plist->postings) {
                double tf = posting.term_freq;
                double dl = index_.docLength(posting.doc_id);
                double norm = 1.0 - b + b * (dl / avgDL);
                double tf_norm = (tf * (k1 + 1.0)) / (tf + k1 * norm);
                scores[posting.doc_id] += idf * tf_norm;
            }
        }
        return scores;
    }

private:
    const InvertedIndex& index_;
};
```

### 5.5 Query Engine

```cpp
// query_engine.h
#pragma once
#include <string>
#include <vector>
#include <queue>
#include <utility>
#include "inverted_index.h"
#include "bm25.h"
#include "tokenizer.h"

struct SearchResult {
    uint32_t doc_id;
    double score;
    bool operator<(const SearchResult& other) const {
        return score < other.score; // max-heap
    }
};

class QueryEngine {
public:
    QueryEngine(InvertedIndex& index)
        : index_(index), scorer_(index), tokenizer_() {}

    // Returns top-K results sorted by score descending
    std::vector<SearchResult> search(const std::string& query, int top_k = 10) {
        auto terms = tokenizer_.tokenize(query);
        auto scores = scorer_.score(terms);

        std::priority_queue<SearchResult> pq;
        for (auto& [doc_id, score] : scores) {
            pq.push({doc_id, score});
        }

        std::vector<SearchResult> results;
        while (!pq.empty() && (int)results.size() < top_k) {
            results.push_back(pq.top());
            pq.pop();
        }
        return results;
    }

private:
    InvertedIndex& index_;
    BM25Scorer scorer_;
    Tokenizer tokenizer_;
};
```

### 5.6 Simple Thread Pool for Crawler

```cpp
// thread_pool.h
#pragma once
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>

class ThreadPool {
public:
    ThreadPool(size_t num_threads) : stop_(false) {
        for (size_t i = 0; i < num_threads; ++i) {
            workers_.emplace_back([this] {
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(queue_mutex_);
                        cv_.wait(lock, [this] {
                            return stop_ || !tasks_.empty();
                        });
                        if (stop_ && tasks_.empty()) return;
                        task = std::move(tasks_.front());
                        tasks_.pop();
                    }
                    task();
                }
            });
        }
    }

    template<typename F>
    void enqueue(F&& f) {
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            tasks_.emplace(std::forward<F>(f));
        }
        cv_.notify_one();
    }

    ~ThreadPool() {
        { std::unique_lock<std::mutex> lock(queue_mutex_); stop_ = true; }
        cv_.notify_all();
        for (auto& t : workers_) t.join();
    }

private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex queue_mutex_;
    std::condition_variable cv_;
    bool stop_;
};
```

---

## 7. Scalability & Distributed Design

This is where Amazon interviews go deep. Be ready to discuss all of this.

### Index Partitioning Strategies

**Option A — Term Partitioning (by term)**
```
Node 1: terms A–F
Node 2: terms G–M
Node 3: terms N–Z
```
- Pro: Simple routing, each node handles full posting list for its terms
- Con: Hot spots for common terms, complex for multi-term queries

**Option B — Document Partitioning (by document range)**
```
Node 1: docs 0–1M
Node 2: docs 1M–2M
Node 3: docs 2M–3M
```
- Pro: Queries broadcast to all nodes, results merged — naturally parallelizes
- Con: More network traffic per query
- ✅ This is how most real search engines work (Elasticsearch shards)

### Replication
- Each shard has N replicas (typically 3) for fault tolerance
- Leader-follower replication for index writes
- Any replica can serve reads (load balancing)

### Caching
```
L1: In-process LRU cache for hot query results (~1K queries)
L2: Redis/Memcached for distributed query cache
L3: Inverted index hot terms cached in memory (skip disk I/O)
```

### Query Routing Architecture
```
Client → Load Balancer → Query Router
                              ↓ (scatter)
              ┌───────────────┼───────────────┐
           Shard 1         Shard 2         Shard 3
              └───────────────┼───────────────┘
                              ↓ (gather & merge)
                         Query Router
                              ↓
                          Client
```

### Handling Scale Numbers (have these ready)
| Metric | Number | Implication |
|--------|--------|------------|
| Web pages indexed | ~50 billion | Need distributed index |
| Index size (compressed) | ~100 TB | Cannot fit on one machine |
| Queries per second | ~100,000 | Need massive parallelism |
| Latency target | <100ms | Cache everything hot |
| Crawler throughput | Billions/day | Need distributed crawling |

---

## 8. Amazon Interview Tips

### Leadership Principles to Weave In
- **Customer Obsession** — "I'd prioritize result relevance because end-users care about finding the right answer, not just fast answers."
- **Dive Deep** — Be ready to go from system architecture down to the BM25 formula.
- **Invent and Simplify** — "For the first version, I'd skip JavaScript rendering and focus on correctness of static HTML."
- **Bias for Action** — "I'd start with a small corpus locally, validate the ranking quality, then scale."

### Common Interview Questions for This Design
1. How would you handle index updates when a page changes? *(Delta indexing, re-crawl scheduling)*
2. How do you deal with near-duplicate pages? *(SimHash / MinHash)*
3. How would you scale the crawler to 1B pages/day? *(Distributed URL frontier, Kafka)*
4. How do you handle malicious or spam pages? *(Link analysis, spam signals)*
5. What happens if one index shard goes down? *(Replicas, graceful degradation)*
6. How do you keep the index fresh? *(Tiered indexing: hot/warm/cold)*
7. How would you implement autocomplete? *(Trie + prefix lookup + frequency ranking)*
8. What's your approach to ranking personalization? *(User context signals — mention privacy tradeoffs)*

### Things to Always Say in the Interview
- Clarify the scope first: "Are we building for the web, or an internal document search?"
- Start with the simplest design, then evolve: "Let me start with a single machine design first..."
- Mention tradeoffs explicitly: "The tradeoff here is consistency vs. availability — I'd prefer availability for search."
- Quantify your assumptions: "Assuming 10B documents at ~1KB each, that's 10TB of raw text..."

---

## 9. Project File Structure

```
search-engine/
├── CMakeLists.txt
├── README.md
├── data/
│   ├── stopwords.txt
│   └── corpus/             # sample documents for testing
├── include/
│   ├── crawler/
│   │   ├── crawler.h
│   │   ├── html_parser.h
│   │   └── url_frontier.h
│   ├── indexer/
│   │   ├── indexer.h
│   │   ├── inverted_index.h
│   │   ├── posting.h
│   │   └── tokenizer.h
│   ├── query/
│   │   ├── query_engine.h
│   │   └── query_parser.h
│   ├── ranking/
│   │   ├── bm25.h
│   │   └── pagerank.h
│   ├── storage/
│   │   └── document_store.h
│   └── utils/
│       ├── thread_pool.h
│       ├── lru_cache.h
│       └── bloom_filter.h
├── src/
│   ├── crawler/
│   ├── indexer/
│   ├── query/
│   ├── ranking/
│   ├── storage/
│   └── main.cpp
└── tests/
    ├── test_tokenizer.cpp
    ├── test_indexer.cpp
    ├── test_bm25.cpp
    └── test_query_engine.cpp
```

---

## 10. Libraries & Tools

| Library | Purpose | Install |
|---------|---------|---------|
| `libexpat` | SAX XML streaming parser for Wikipedia dump | `apt install libexpat1-dev` |
| `libbz2` | Decompress `.bz2` dump on the fly | `apt install libbz2-dev` |
| `libcurl` | HTTP requests (useful if extending to crawl later) | `apt install libcurl4-openssl-dev` |
| `gumbo-parser` | HTML parsing (Google's) | `apt install libgumbo-dev` |
| `GoogleTest` | Unit testing | CMake FetchContent |
| `spdlog` | Fast logging | CMake FetchContent |
| `nlohmann/json` | Config files / API responses | Header-only |
| `xxHash` | Ultra-fast hashing for dedup | Header-only |

### CMakeLists.txt starter
```cmake
cmake_minimum_required(VERSION 3.20)
project(SearchEngine CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Compiler flags
add_compile_options(-Wall -Wextra -O2)

find_package(CURL REQUIRED)
find_package(Threads REQUIRED)

include_directories(include)

add_executable(search_engine
    src/main.cpp
    src/indexer/tokenizer.cpp
    src/indexer/inverted_index.cpp
    src/ranking/bm25.cpp
    src/query/query_engine.cpp
    src/crawler/crawler.cpp
)

target_link_libraries(search_engine
    CURL::libcurl
    Threads::Threads
)
```

---

## Quick Reference Cheat Sheet

```
Inverted Index lookup:  O(1) hash map
Posting list merge:     O(n + m) two-pointer
Top-K ranking:          O(N log K) min-heap
BM25 scoring:           O(|query| × avg_posting_length)
PageRank:               O(iterations × edges)
URL dedup (Bloom):      O(1) insert/lookup, probabilistic

Space:
  Raw index:            ~20% of corpus size (with compression)
  Posting list entry:   ~12 bytes (docID + tf + offset)
  Doc store entry:      ~500 bytes average
```

