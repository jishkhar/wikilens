# WikiLens

WikiLens is a full-stack Wikipedia search engine built with a C++ backend and a React frontend. It parses Wikipedia dumps, strips Wikitext, builds a persistent inverted index, serves BM25-ranked results, and supports offline PageRank recomputation from a saved link graph.

## Stack

- Backend: C++20, Crow, Expat, BZip2
- Frontend: React 19, TypeScript, Vite, Axios, Framer Motion
- Corpus: Wikipedia XML dumps, including `.xml.bz2`

## Current Capabilities

- Streams plain XML and bz2-compressed Wikipedia dumps
- Cleans Wikitext before indexing
- Builds and persists an inverted index and document store
- Saves the internal link graph as a plain-text edge list
- Supports offline PageRank recomputation from cached artifacts
- Exposes `/search`, `/suggest`, and `/stats`
- Caches repeated search queries with an in-process LRU cache
- Supports query benchmarking and indexing-stage profiling from the backend CLI
- Ships a Vite-powered frontend with `/api` proxy support, suggestions, loading states, error states, keyboard shortcut focus, and pagination

## Repository Guide

- Frontend setup and dev workflow: [frontend/README.md](/home/nyx/Projects/wikilens/frontend/README.md)
- Backend build, runtime flags, cache artifacts, and API notes: [backend/README.md](/home/nyx/Projects/wikilens/backend/README.md)
- AWS/container deployment guide: [AWS_DEPLOYMENT.md](/home/nyx/Projects/wikilens/AWS_DEPLOYMENT.md)

## Quick Start

### 1. Build the backend

```bash
cd backend
cmake -S . -B build
cmake --build build -j"$(nproc)"
```

### 2. Run the backend

From `backend/build`:

```bash
./wikilens
```

Useful variants:

```bash
./wikilens --dump /path/to/simplewiki-latest-pages-articles.xml.bz2
./wikilens --dump /path/to/dump.xml --max-docs 50000
./wikilens --recompute-pagerank
./wikilens --compute-pagerank
./wikilens --benchmark
./wikilens --dump ../simplewiki-latest-pages-articles.xml.1 --max-docs 10001 --profile-indexing
```

### 3. Run the frontend

```bash
cd frontend
npm install
npm run dev
```

Open `http://localhost:5173`.

The frontend uses `/api` by default and Vite proxies that to `http://localhost:8080` in development.

On first backend startup, WikiLens may need to parse the dump and write cache artifacts. Later runs reuse the saved index and document store from `backend/data/cache`.

## API

### `GET /search`

Example:

```bash
curl "http://localhost:8080/search?q=computer&limit=5"
```

### `GET /suggest`

Example:

```bash
curl "http://localhost:8080/suggest?q=comp&limit=5"
```

### `GET /stats`

Example:

```bash
curl "http://localhost:8080/stats"
```

See [backend/README.md](/home/nyx/Projects/wikilens/backend/README.md) for the full API notes and runtime flags.

## Performance Snapshot

Measured results from the current backend build are recorded in [backend/PERFORMANCE.md](/home/nyx/Projects/wikilens/backend/PERFORMANCE.md).

Current snapshot:

- full cached Simple English run: `549,680` indexed documents
- benchmark mode: `9.105 ms` average uncached latency, `24.664 ms` p95
- warm-cache benchmark: `0.001 ms` average latency
- profiled capped ingestion over `10,001` docs: `3.13 s` total, `173.3 MiB` RSS

## Deployment Path

For containerized deployment on AWS EC2, follow [AWS_DEPLOYMENT.md](/home/nyx/Projects/wikilens/AWS_DEPLOYMENT.md).

The intended production shape is:

- frontend served behind Nginx
- backend exposed internally on port `8080`
- frontend proxied to backend through `/api`
- dump file and cache artifacts stored on persistent host volumes

## Current Limitations

- No automated test suite is checked in yet
- Full-dump indexing still loads the searchable structures into memory at runtime, so larger corpora need meaningful RAM headroom
- Benchmarking and profiling are manual CLI workflows, not an automated regression suite
- Some deployment docs may need periodic sync as runtime flags evolve
