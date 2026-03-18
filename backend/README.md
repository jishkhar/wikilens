# Backend

WikiLens backend is a C++20 search service built on Crow. It parses Wikipedia dumps, builds an inverted index, stores document metadata, and serves search results over HTTP.

## Features

- Streams plain XML dumps and `.xml.bz2` dumps
- Strips Wikitext before tokenization
- Builds an in-memory inverted index with BM25 scoring
- Stores document metadata, snippets, URLs, and PageRank
- Persists index and document store caches to `backend/data/cache`
- Saves the extracted link graph as a plain-text edge list
- Supports offline PageRank recomputation from cached artifacts
- Exposes `/search` and `/stats`

## Dependencies

Required system packages:

- `cmake`
- `expat`
- `asio`
- `bzip2`
- a C++20-capable compiler

On Arch-based systems:

```bash
sudo pacman -S cmake expat asio bzip2
```

## Build

```bash
cd backend
cmake -S . -B build
cmake --build build -j"$(nproc)"
```

The binary is written to `backend/build/wikilens`.

## Dump Files

By default, the backend looks for:

```text
../simplewiki-latest-pages-articles.xml.1
```

relative to `backend/build`.

You can override that with:

- `--dump /path/to/dump.xml`
- `WIKILENS_DUMP_PATH=/path/to/dump.xml`

Both plain XML and `.xml.bz2` dumps are supported.

## Runtime Options

```bash
./wikilens [options]
```

Supported options:

- `--dump <path>`: override the dump file path
- `--max-docs <count>`: stop indexing after `count` documents
- `--compute-pagerank`: compute PageRank during indexing
- `--recompute-pagerank`: load cached store + saved edge list, recompute PageRank, save, then exit

Supported environment variables:

- `WIKILENS_DUMP_PATH`
- `WIKILENS_MAX_DOCS`
- `WIKILENS_COMPUTE_PAGERANK=1`

## Cache And Link-Graph Artifacts

Artifacts are stored in:

```text
backend/data/cache/
```

Typical files:

- `inverted_index.bin`
- `document_store.bin`
- `link_graph.txt`

For non-default dump paths or capped indexing runs, the backend writes hashed artifact names so different runs do not overwrite each other.

## PageRank Flow

By default, indexing saves the link graph but skips PageRank computation to keep startup cheaper on larger dumps.

To compute PageRank after indexing:

```bash
cd backend/build
./wikilens --recompute-pagerank
```

If you indexed a custom dump or a capped corpus, pass the same flags again:

```bash
./wikilens --recompute-pagerank --dump /path/to/dump.xml.bz2 --max-docs 50000
```

If you want the old one-pass behavior, run:

```bash
./wikilens --compute-pagerank
```

## API

### `GET /search`

Parameters:

- `q` required search query
- `limit` optional, default `10`, max `100`
- `offset` optional, default `0`

Example:

```bash
curl "http://localhost:8080/search?q=alan+turing&limit=5"
```

Response shape:

```json
{
  "results": [
    {
      "title": "Alan Turing",
      "url": "https://en.wikipedia.org/wiki/Alan_Turing",
      "snippet": "Alan Mathison Turing was an English mathematician...",
      "score": 0.91,
      "pagerank": 0.42
    }
  ]
}
```

Validation behavior:

- missing or blank `q` returns HTTP `400`
- invalid `limit` or `offset` returns HTTP `400`
- queries over 512 characters return HTTP `400`

### `GET /stats`

Example:

```bash
curl "http://localhost:8080/stats"
```

Response shape:

```json
{
  "total_docs": 20000,
  "avg_doc_length": 83.4,
  "term_count": 152938
}
```

## Running

From `backend/build`:

```bash
./wikilens
```

The backend serves HTTP on port `8080`.

On first run it may build the index from the dump. On later runs it will reuse cache artifacts when the matching cache files already exist.
