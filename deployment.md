# WikiLens Deployment Guide For AWS With Docker

This document is based on the current repository state, not on a generic React/C++ template. The recommended deployment target for this codebase today is:

- one Amazon EC2 instance
- Docker Engine + Docker Compose
- one `backend` container for the C++ Crow API
- one `frontend` container for Nginx + the built React app
- one persistent EBS-backed host path for dump files and cache artifacts

That is the best fit for the repo because the backend is stateful at startup, writes cache files locally, reads a local Wikipedia dump, keeps the searchable index in memory, and currently serves on a fixed internal port of `8080`.

## 1. What I Audited In This Repo

I reviewed the project-owned files end to end and checked the built entrypoints.

### Top-Level Files

- `.gitignore`: excludes generated dump files, cache files, build output, and frontend `dist`; this confirms dump/cache are runtime data and should live on persistent volumes, not in git or in the image.
- `.gitmodules`: `backend/external/Crow` is a git submodule, so any source-based AWS build must clone with `--recurse-submodules` or run `git submodule update --init --recursive`.
- `README.md`: confirms the intended production shape is frontend behind Nginx, backend on `8080`, and persistent dump/cache storage.
- `plan.md`: architecture/planning document; useful background, but not a deployment source of truth.
- `Todo.md`: no active backend-quality TODOs that block deployment.

### Frontend Files

- `frontend/package.json`: build uses `tsc -b && vite build`; runtime is static assets only.
- `frontend/bun.lock`: dependency lock exists for Bun, but there is no committed `package-lock.json`; if you use `npm` inside Docker, use `npm install`, not `npm ci`, unless you first add a lockfile.
- `frontend/vite.config.ts`: `/api` is only proxied to `http://localhost:8080` in local development; production still needs a reverse proxy.
- `frontend/src/App.tsx`: production-safe because `VITE_API_BASE_URL` defaults to `/api`, so the frontend works correctly behind Nginx without code changes.
- `frontend/src/main.tsx`: standard React bootstrap.
- `frontend/src/index.css`: styling only; no deployment impact.
- `frontend/index.html`: static app shell only.
- `frontend/eslint.config.js`, `frontend/tsconfig*.json`: build/lint configuration only.
- `frontend/public/favicon.svg`: static asset only.
- `frontend/README.md`: already describes the intended production routing model of `/api`.

### Backend Config And Docs

- `backend/CMakeLists.txt`: builds a single `wikilens` binary; compile-time dependencies are Expat, BZip2, pthread, Asio headers, and the vendored Crow headers.
- `backend/README.md`: documents runtime flags and confirms the backend reads a dump file, writes cache to `backend/data/cache`, and serves on port `8080`.
- `backend/PERFORMANCE.md`: useful for sizing. The checked-in snapshot shows cached Simple English search is fast, but the backend is memory-heavy enough that EC2 sizing matters.

### Backend Data Files

- `backend/data/stopwords.txt`: required at runtime because the backend constructs `Tokenizer("../data/stopwords.txt")`. The runtime image must contain this file at `/app/backend/data/stopwords.txt` if the working directory is `/app/backend/build`.
- `backend/data/corpus/doc1.txt` through `backend/data/corpus/doc10.txt`: sample text files; they are not referenced by the current runtime path and are not required for deployment.

### Backend Source Files

- `backend/src/main.cpp`: the critical deployment file. It confirms:
  - the default dump path is `../simplewiki-latest-pages-articles.xml.1` relative to `backend/build`
  - the backend also supports `--dump` and `WIKILENS_DUMP_PATH`, which is the better Docker/AWS option
  - cache artifacts are written to `../data/cache`
  - stopwords are read from `../data/stopwords.txt`
  - the backend serves HTTP on fixed port `8080`
  - the backend exposes `/`, `/search`, `/suggest`, and `/stats`
  - cache loading happens before the server starts
  - if cache files do not exist, the container will parse the dump and build them on first boot
  - PageRank can be computed during indexing or later with `--recompute-pagerank`
  - query-result caching is in-process only
- `backend/src/wiki_parser.cpp`: supports both plain XML and `.xml.bz2`, so on AWS you can keep the compressed dump and point `WIKILENS_DUMP_PATH` at it instead of expanding the file.
- `backend/src/wikitext_stripper.cpp`: extracts internal links and cleaned text during ingestion; this is why the link-graph file also needs to persist if you want offline PageRank recomputation later.
- `backend/src/tokenizer.cpp`, `backend/src/porter_stemmer.cpp`: confirm stopwords/stemming are local-process concerns only.
- `backend/src/inverted_index.cpp`, `backend/src/document_store.cpp`: persist the backend cache into binary files; these files must survive container restarts.
- `backend/src/query_engine.cpp`, `backend/src/bm25.cpp`, `backend/src/pageranker.cpp`: confirm the query path is fully in-process and memory-resident after startup.
- `backend/include/*.h`: match the same runtime shape; no extra deployment surprises.

### Vendored Dependency

- `backend/external/Crow/**`: audited as an upstream vendored dependency rather than line-by-line deployment logic. The deployment implication is simple:
  - the submodule must be present at build time
  - Crow is effectively being consumed as headers from `external/Crow/include`
  - there is no separate Crow service to deploy

### Runtime Validation I Performed

- `backend/build/wikilens --help` runs and matches the documented flags.
- `frontend/npm run build` succeeds.
- `backend/build/wikilens --benchmark` loads the current default cache successfully.

Observed from the current workspace:

- current Simple English dump file on disk: about `1.6G`
- current cache directory on disk: about `863M`
- current cached backend load for `549,680` docs: about `1984.9 MiB RSS`

Those observed numbers are the reason this guide recommends persistent storage and a non-tiny instance.

## 2. Recommended AWS Architecture

Use this first:

- Amazon EC2 instance running Ubuntu 24.04 LTS
- one additional gp3 EBS volume or sufficient root EBS space
- Docker Engine + Docker Compose plugin
- optional Route 53 + ALB + ACM for HTTPS

Traffic shape:

`Browser -> ALB or EC2 public IP -> frontend container (Nginx :80) -> backend container (:8080 internal only)`

Persistent storage shape:

- `/srv/wikilens/dumps`: Wikipedia dump files
- `/srv/wikilens/cache`: backend cache artifacts and link graph

Why EC2 is the right first deployment target for this repo:

- the backend expects local files and local writable cache
- startup is stateful and potentially long
- the index is loaded into process memory
- the app is not yet structured around external object storage, managed databases, or stateless task workers
- Compose on EC2 keeps the operational model simple while matching the current code closely

## 3. AWS Sizing Recommendation

For the current checked-in Simple English workflow:

- start with at least `t3.large` or `m7i.large`
- use at least `50-80 GB` of gp3 storage if you plan to keep dump files, cache files, logs, rebuilt images, and room for future reindexing

Why not smaller:

- the backend alone already loaded at roughly `2 GiB RSS` against the cached Simple English index in this workspace
- first-run indexing needs additional headroom
- Docker, Ubuntu, and Nginx need space too

For full English Wikipedia:

- do not treat this EC2 shape as production-ready without more testing
- expect much larger RAM, storage, and indexing time requirements
- consider building the index on a larger box, persisting artifacts, and serving only from prebuilt cache

## 4. Files You Should Add For Docker Deployment

The repo does not currently include Docker build assets, so add the following.

### `backend/.dockerignore`

This prevents Docker from sending huge runtime data into the build context.

```dockerignore
build
data/cache
data/corpus
simplewiki-latest-pages-articles.xml*
enwiki-*-pages-articles*.xml*
*.log
```

### `backend/Dockerfile`

This image compiles the binary in one stage and ships only the runtime bits plus `stopwords.txt`.

```dockerfile
FROM ubuntu:24.04 AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    libasio-dev \
    libbz2-dev \
    libexpat1-dev \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app/backend

COPY CMakeLists.txt ./
COPY include ./include
COPY src ./src
COPY data/stopwords.txt ./data/stopwords.txt
COPY external/Crow ./external/Crow

RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && \
    cmake --build build -j"$(nproc)"

FROM ubuntu:24.04 AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends \
    libbz2-1.0 \
    libexpat1 \
    ca-certificates \
    curl \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app/backend/build

COPY --from=build /app/backend/build/wikilens /app/backend/build/wikilens
COPY --from=build /app/backend/data/stopwords.txt /app/backend/data/stopwords.txt

RUN mkdir -p /app/backend/data/cache /data/dumps

EXPOSE 8080

CMD ["./wikilens"]
```

Why this layout matters:

- `main.cpp` reads `../data/stopwords.txt`, so the runtime working directory must be `backend/build`
- `main.cpp` writes cache to `../data/cache`, so that path must exist and be writable
- the dump should be mounted from the host, not copied into the image

### `frontend/.dockerignore`

```dockerignore
node_modules
dist
*.log
```

### `frontend/Dockerfile`

Use Node LTS to build, then Nginx to serve.

```dockerfile
FROM node:22-alpine AS build

WORKDIR /app

COPY package.json bun.lock ./
RUN npm install

COPY index.html ./
COPY tsconfig.json tsconfig.app.json tsconfig.node.json ./
COPY vite.config.ts eslint.config.js ./
COPY public ./public
COPY src ./src

RUN npm run build

FROM nginx:1.27-alpine

COPY nginx.conf /etc/nginx/conf.d/default.conf
COPY --from=build /app/dist /usr/share/nginx/html

EXPOSE 80

CMD ["nginx", "-g", "daemon off;"]
```

Note on package management:

- this uses `npm install` because the repo currently does not commit `package-lock.json`
- if you want fully reproducible npm builds, add and commit a lockfile first

### `frontend/nginx.conf`

This is required because Vite dev proxy does not exist in production.

```nginx
server {
    listen 80;
    server_name _;

    root /usr/share/nginx/html;
    index index.html;

    location / {
        try_files $uri /index.html;
    }

    location /api/ {
        proxy_pass http://backend:8080/;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
    }
}
```

Why this works with the current frontend:

- `frontend/src/App.tsx` defaults `API_BASE_URL` to `/api`
- the Nginx rule strips the browser-visible host problem away and keeps the frontend/backend same-origin

### `docker-compose.yml`

Put this at the repo root.

```yaml
services:
  backend:
    build:
      context: ./backend
      dockerfile: Dockerfile
    container_name: wikilens-backend
    restart: unless-stopped
    environment:
      WIKILENS_DUMP_PATH: /data/dumps/simplewiki-latest-pages-articles.xml.bz2
      WIKILENS_MAX_DOCS: "0"
    volumes:
      - /srv/wikilens/dumps:/data/dumps:ro
      - /srv/wikilens/cache:/app/backend/data/cache
    expose:
      - "8080"

  frontend:
    build:
      context: ./frontend
      dockerfile: Dockerfile
    container_name: wikilens-frontend
    restart: unless-stopped
    depends_on:
      - backend
    ports:
      - "80:80"
```

Important details:

- `8080` is not published to the internet
- only the frontend publishes port `80`
- the backend reads the compressed dump from the host path via `WIKILENS_DUMP_PATH`
- the cache directory is persisted outside the container, so a restart does not trigger reindexing unless the cache is missing

## 5. Step-By-Step AWS Deployment

### Step 1: Launch The EC2 Instance

Recommended baseline:

- AMI: Ubuntu Server `24.04 LTS`
- instance type: `t3.large` minimum for current Simple English usage
- storage: `50-80 GB` gp3 minimum

Security group inbound rules:

- `22/tcp` from your IP only, or use AWS Systems Manager Session Manager instead
- `80/tcp` from `0.0.0.0/0`
- `443/tcp` from `0.0.0.0/0` only if you later terminate TLS on the instance

Do not expose `8080` publicly.

### Step 2: Install Docker And Git

SSH to the instance:

```bash
ssh -i /path/to/key.pem ubuntu@<EC2_PUBLIC_IP>
```

Install Docker Engine, Compose plugin, and Git:

```bash
sudo apt update
sudo apt install -y ca-certificates curl gnupg git

sudo install -m 0755 -d /etc/apt/keyrings
curl -fsSL https://download.docker.com/linux/ubuntu/gpg | \
  sudo gpg --dearmor -o /etc/apt/keyrings/docker.gpg
sudo chmod a+r /etc/apt/keyrings/docker.gpg

echo \
  "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/docker.gpg] \
  https://download.docker.com/linux/ubuntu \
  $(. /etc/os-release && echo $VERSION_CODENAME) stable" | \
  sudo tee /etc/apt/sources.list.d/docker.list > /dev/null

sudo apt update
sudo apt install -y docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin

sudo usermod -aG docker $USER
newgrp docker

docker --version
docker compose version
```

### Step 3: Clone The Repository Correctly

Because Crow is a submodule, clone like this:

```bash
cd /opt
sudo git clone --recurse-submodules <YOUR_REPOSITORY_URL> wikilens
sudo chown -R $USER:$USER /opt/wikilens
cd /opt/wikilens
```

If you already cloned without submodules:

```bash
git submodule update --init --recursive
```

### Step 4: Create Persistent Host Directories

```bash
sudo mkdir -p /srv/wikilens/dumps /srv/wikilens/cache
sudo chown -R $USER:$USER /srv/wikilens
```

If you attach a separate EBS volume, mount it first and then create those directories on that mounted filesystem.

### Step 5: Add The Docker Files

Create the files from Section 4:

- `backend/.dockerignore`
- `backend/Dockerfile`
- `frontend/.dockerignore`
- `frontend/Dockerfile`
- `frontend/nginx.conf`
- `docker-compose.yml`

### Step 6: Download The Dump File

Because the backend supports `.xml.bz2`, keep the compressed file and save disk.

```bash
cd /srv/wikilens/dumps
wget https://dumps.wikimedia.org/simplewiki/latest/simplewiki-latest-pages-articles.xml.bz2
```

If you want to use an uncompressed file instead, that also works, but then update `WIKILENS_DUMP_PATH` accordingly.

### Step 7: Build And Start The Stack

```bash
cd /opt/wikilens
docker compose build
docker compose up -d
```

Check status:

```bash
docker compose ps
docker compose logs -f backend
docker compose logs -f frontend
```

What to expect on first backend startup:

- the backend will not serve traffic until it finishes cache load or initial indexing
- if `/srv/wikilens/cache` is empty, it will parse the dump and build the cache first
- this may take a while depending on instance size and corpus size

### Step 8: Optional One-Time PageRank Recompute

The current backend can skip PageRank during the initial indexing path and recompute it later from persisted artifacts.

Run this one-off command after the initial cache exists:

```bash
cd /opt/wikilens
docker compose run --rm backend ./wikilens --recompute-pagerank
```

Then restart the backend so the serving process reloads the updated document store:

```bash
docker compose restart backend
```

### Step 9: Verify The Deployment

From the EC2 host:

```bash
curl http://127.0.0.1/
curl http://127.0.0.1/api/stats
curl "http://127.0.0.1/api/search?q=alan+turing&limit=3"
curl "http://127.0.0.1/api/suggest?q=comp&limit=5"
```

From your browser:

```text
http://<EC2_PUBLIC_IP>/
```

If you later put an ALB or domain name in front, replace the public IP with that hostname.

## 6. HTTPS And AWS-Native Fronting

The simplest path is direct EC2 on port `80`, but the better AWS shape is:

- Route 53 for DNS
- Application Load Balancer in front of the instance
- ACM certificate on the ALB
- ALB listener `443 -> target group -> EC2 instance port 80`

Why this is a good fit:

- you keep TLS termination out of the containers
- certificate renewal is handled by ACM
- you can add health checks at the ALB level

Recommended ALB health check path:

- `/api/stats`

That path proves:

- frontend Nginx is reachable
- Nginx can proxy to backend
- backend is alive and has loaded enough to answer

## 7. Operations After Deployment

### Rebuild After Code Changes

```bash
cd /opt/wikilens
git pull --recurse-submodules
docker compose up -d --build
```

### Restart Without Rebuilding

```bash
cd /opt/wikilens
docker compose restart
```

### Stop The Stack

```bash
cd /opt/wikilens
docker compose down
```

### Inspect Logs

```bash
cd /opt/wikilens
docker compose logs -f backend
docker compose logs -f frontend
```

### Back Up The Persistent Data

At minimum, back up:

- `/srv/wikilens/dumps`
- `/srv/wikilens/cache`

On AWS, the cleanest option is EBS snapshots of the volume holding those paths.

### When To Clear Cache

Clear `/srv/wikilens/cache` when:

- indexing logic changes
- tokenization/stripping logic changes
- ranking persistence format changes
- you switch to a different dump file and want a clean rebuild

Example:

```bash
rm -f /srv/wikilens/cache/*
cd /opt/wikilens
docker compose restart backend
```

Only do that intentionally, because the next backend boot will reindex from scratch.

## 8. Current Deployment Risks And Constraints In The Codebase

These are important and come directly from the current implementation.

- The backend port is fixed at `8080`; there is no environment variable for port override.
- The backend uses relative filesystem paths internally, so container working directory matters.
- There is no dedicated lightweight `/healthz` endpoint; `/stats` is the best current health target.
- The backend is stateful at boot and can take time before becoming ready.
- The query cache is process-local and disappears on backend restart.
- The frontend build path is deterministic, but the npm install step is less reproducible until a `package-lock.json` is committed.
- The Crow dependency is a submodule, so shallow or incomplete clones will break backend image builds.
- Full English Wikipedia deployment is a separate capacity project because the current code keeps the searchable structures in memory.

## 9. Recommended Future Improvements Before A Larger AWS Rollout

- Commit Docker assets to the repo so deployment is repeatable.
- Add a dedicated `/healthz` endpoint that does not require full stats serialization.
- Add `WIKILENS_PORT` support so the backend is less opinionated in container environments.
- Commit a frontend `package-lock.json` or switch fully to Bun in CI and Docker.
- Add CloudWatch log shipping or a container log forwarder.
- Add a systemd unit on EC2 if you want `docker compose up -d` guaranteed after host reboot.
- Add CI to build and publish images to ECR if you later move from source-based EC2 builds to image-based deployments.

## 10. If You Want To Move Beyond Single-Host EC2 Later

A stronger AWS production shape later would be:

- frontend static assets on S3 + CloudFront
- backend image in ECR
- backend service on ECS EC2 or ECS with an EBS/EFS-aware design
- ALB for `/api`
- offline indexing job that writes cache artifacts to durable storage before service rollout

I do not recommend jumping straight to that with the current codebase because the present runtime still assumes local files and local persistent cache.
