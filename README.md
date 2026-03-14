# WikiLens

A full-stack Wikipedia search engine built from scratch in C++ and React. WikiLens parses Wikipedia XML dumps, builds an inverted index, ranks results with BM25 + PageRank, and serves them through a REST API consumed by a modern React frontend.

---

## Architecture

```
┌─────────────────────────────────────────────┐
│               React Frontend                │
│   (Vite + TypeScript + Framer Motion)       │
│              localhost:5173                 │
└────────────────────┬────────────────────────┘
                     │ HTTP (Axios)
                     ▼
┌─────────────────────────────────────────────┐
│          Crow HTTP Server (C++20)           │
│              localhost:8080                 │
│                                             │
│  /search?q=...&limit=...&offset=...         │
└────────────────────┬────────────────────────┘
                     │
          ┌──────────▼──────────┐
          │    Query Engine     │
          │  BM25 + PageRank    │
          └──────────┬──────────┘
                     │
     ┌───────────────┼───────────────┐
     ▼               ▼               ▼
┌─────────┐   ┌──────────────┐  ┌──────────────┐
│Inverted │   │ Document     │  │  PageRank    │
│ Index   │   │ Store        │  │  Scores      │
└─────────┘   └──────────────┘  └──────────────┘
     ▲
     │
┌────┴───────────────────────────────────────┐
│              Indexing Pipeline             │
│                                            │
│  XML Dump → WikiParser (libexpat)          │
│           → WikitextStripper               │
│           → Tokenizer + Porter Stemmer     │
│           → InvertedIndex + DocumentStore  │
│           → Link Graph → PageRank          │
└────────────────────────────────────────────┘
```

---

## Features

### Backend (C++20)
- **Wikipedia XML parsing** — streams `simplewiki` or `enwiki` dump files via libexpat with zero full-file load into memory
- **Wikitext stripping** — removes templates `{{...}}`, internal links `[[...]]`, references `<ref>`, headings, bold/italic markers, and HTML comments; extracts internal link targets for the link graph
- **Tokenizer** — lowercases, strips punctuation, removes stopwords, and applies Porter stemming
- **Inverted index** — maps stemmed terms to posting lists with per-document term frequencies
- **BM25 scoring** — tunable `k1 = 1.5`, `b = 0.75`; blended with PageRank at query time
- **PageRank** — iterative power method (damping factor 0.85, 30 iterations) over the internal Wikipedia link graph; scores are log-normalised
- **Persistent index cache** — after first build, serializes the inverted index + document store to disk and reloads them on next start (no full re-index on every run)
- **Crow REST API** — multithreaded HTTP server with CORS headers; returns `title`, `url`, `snippet`, `score`, and `pagerank` per result; supports `limit` and `offset` for pagination

### Frontend (React 19 + TypeScript + Vite)
- Glassmorphism UI with dark gradient background
- Animated results via **Framer Motion**
- Clickable result titles that open the live Wikipedia article in a new tab
- Snippet displayed below each result
- "Load more" pagination
- Enter-to-search keyboard support

---

## Project Structure

```
wikilens/
├── backend/
│   ├── CMakeLists.txt
│   ├── include/           # Header files
│   │   ├── bm25.h
│   │   ├── document_store.h
│   │   ├── inverted_index.h
│   │   ├── pagerank.h
│   │   ├── porter_stemmer.h
│   │   ├── query_engine.h
│   │   ├── tokenizer.h
│   │   ├── wiki_parser.h
│   │   └── wikitext_stripper.h
│   ├── src/               # Implementation files
│   │   ├── main.cpp       # Indexing pipeline + Crow HTTP server
│   │   ├── bm25.cpp
│   │   ├── document_store.cpp
│   │   ├── inverted_index.cpp
│   │   ├── pageranker.cpp
│   │   ├── porter_stemmer.cpp
│   │   ├── query_engine.cpp
│   │   ├── tokenizer.cpp
│   │   ├── wiki_parser.cpp
│   │   └── wikitext_stripper.cpp
│   ├── data/
│   │   ├── stopwords.txt
│   │   └── corpus/        # Sample documents
│   └── external/
│       └── Crow/          # Bundled Crow HTTP library
└── frontend/
    ├── src/
    │   ├── App.tsx        # Main search UI
    │   ├── main.tsx
    │   └── index.css
    ├── package.json
    └── vite.config.ts
```

---

## Dependencies

### Backend
| Dependency | Purpose | Install |
|---|---|---|
| **CMake ≥ 3.20** | Build system | `sudo pacman -S cmake` |
| **libexpat** | XML stream parsing | `sudo pacman -S expat` |
| **ASIO** (standalone) | Async I/O for Crow | `sudo pacman -S asio` |
| **pthreads** | Multithreaded server | bundled with glibc |
| **Crow** | HTTP framework | bundled in `external/Crow/` |

### Frontend
| Dependency | Purpose |
|---|---|
| **Node.js / npm** | Package management |
| **React 19** | UI framework |
| **Vite** | Dev server & bundler |
| **Axios** | HTTP client |
| **Framer Motion** | Animations |
| **TypeScript** | Type safety |

---

## Getting Started

### 1 — Get a Wikipedia dump

Download the Simple English Wikipedia dump (~250 MB compressed) for development:

```bash
wget https://dumps.wikimedia.org/simplewiki/latest/simplewiki-latest-pages-articles.xml.bz2
bzip2 -d simplewiki-latest-pages-articles.xml.bz2
# Place the extracted .xml file inside backend/ and rename/symlink as needed
```

The parser is pointed at `../simplewiki-latest-pages-articles.xml.1` relative to the build directory. Adjust the path in `backend/src/main.cpp` if your file differs.

### 2 — Build the backend

```bash
cd backend
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

### 3 — Run the backend

```bash
# From backend/build/
./wikilens
```

On first run, the server parses the dump, builds BM25/PageRank data (currently stopping early at 20 000 pages for testing), then writes cache files to `backend/data/cache/`.

On later runs, if these cache files exist, WikiLens loads them directly and starts much faster without rebuilding the index.

If you change dump/version or ranking/indexing logic, delete the cache files and restart:

```bash
rm -f backend/data/cache/inverted_index.bin backend/data/cache/document_store.bin
```

After loading (or rebuilding), the API listens on **port 8080**.

### 4 — Run the frontend

```bash
cd frontend
npm install
npm run dev
```

Open **http://localhost:5173** in your browser.

---

## API

### `GET /search`

| Parameter | Type | Default | Description |
|---|---|---|---|
| `q` | string | — | Search query (required) |
| `limit` | integer | 10 | Max results to return |
| `offset` | integer | 0 | Pagination offset |

**Example response:**

```json
{
  "results": [
    {
      "title": "Alan Turing",
      "url": "https://en.wikipedia.org/wiki/Alan_Turing",
      "snippet": "Alan Mathison Turing was an English mathematician, computer scientist…",
      "score": 18.472,
      "pagerank": 0.931
    }
  ]
}
```

---

## Ranking

The final score for each document is a blend of BM25 and log-normalised PageRank:

$$\text{score}(d, q) = \text{BM25}(d, q) + \lambda \cdot \ln(1 + \text{PR}(d))$$

BM25 parameters: $k_1 = 1.5$, $b = 0.75$.  
PageRank: damping factor $d = 0.85$, 30 power-method iterations.
