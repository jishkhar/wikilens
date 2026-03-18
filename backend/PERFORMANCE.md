# WikiLens Performance Notes

Collected on `2026-03-18` from the current backend build.

## Query Benchmark

Command:

```bash
cd backend/build
./wikilens --benchmark
```

Dataset:

- persisted default cache built from `simplewiki-latest-pages-articles.xml.1`
- `549,680` indexed documents
- benchmark queries: `computer`, `science`, `history`, `new york`, `world war`, `alan turing`, `music`, `football`, `river`, `language`, `mathematics`, `internet`

Results:

- uncached average: `9.105 ms`
- uncached p50: `5.758 ms`
- uncached p95: `24.664 ms`
- uncached max: `26.955 ms`
- warm-cache average: `0.001 ms`
- warm-cache p95: `0.002 ms`
- cache hits during warm run: `12`
- cache misses during warmup: `12`

## Indexing Profile

Command:

```bash
cd backend/build
./wikilens --dump ../simplewiki-latest-pages-articles.xml.1 --max-docs 10001 --profile-indexing
```

Results over the first `10,001` documents:

- total indexing time: `3.13 s`
- memory after indexing: `173.3 MiB RSS`
- Wikitext stripping: `868.78 ms total` (`0.09 ms/doc`)
- tokenization: `710.19 ms total` (`0.07 ms/doc`)
- index/store insertion: `764.48 ms total` (`0.08 ms/doc`)

Current takeaway:

- the ingestion path is reasonably balanced across stripping, tokenization, and index/store insertion
- query-result caching removes nearly all repeat-query latency for repeated normalized searches
