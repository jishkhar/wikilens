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

This section is written as a literal runbook. If you want the fastest safe path, do it in this order:

1. create the EC2 instance
2. create and attach the data EBS volume
3. SSH in and mount the volume
4. install Docker and Git
5. clone the repo with submodules
6. add the Docker files from Section 4
7. download the dump
8. build and start the containers
9. verify over HTTP
10. add ALB + ACM + Route 53 after the raw EC2 deployment works

### Phase A: Create The EC2 Instance In The AWS Console

#### Step A1: Sign In And Choose The Region

1. Sign in to the AWS Management Console.
2. In the top-right region selector, choose the AWS Region where you want everything to live.
3. Keep the EC2 instance, EBS volume, ALB, ACM certificate, and Route 53 alias setup consistent with that region.

Use one region for the whole first deployment. Do not mix resources across regions.

#### Step A2: Open The EC2 Launch Wizard

1. In the AWS Console search bar, type `EC2`.
2. Open `EC2`.
3. In the left sidebar, choose `Instances`.
4. Click `Launch instances`.

#### Step A3: Fill In The Launch Wizard

On the `Launch an instance` page, use these values.

##### Name And Tags

- In `Name and tags`, enter: `wikilens-prod-1`

##### Application And OS Images (AMI)

- In `Application and OS Images (Amazon Machine Image)`, keep `Quick Start`
- Choose `Ubuntu`
- Choose `Ubuntu Server 24.04 LTS`
- Architecture: `64-bit (x86)`

##### Instance Type

- In `Instance type`, select `t3.large`
- If you want a steadier non-burstable option, `m7i.large` is also a good choice

##### Key Pair (Login)

1. In `Key pair (login)`, click `Create new key pair` if you do not already have one.
2. Use:
   - `Key pair name`: `wikilens-prod`
   - `Key pair type`: `RSA`
   - `Private key file format`: `.pem` if you use OpenSSH or macOS/Linux terminal, `.ppk` if you use PuTTY
3. Click `Create key pair`.
4. Your browser downloads the private key file. Store it somewhere safe immediately.

##### Network Settings

1. In `Network settings`, click `Edit`.
2. For `VPC`, use your default VPC unless you already have a dedicated one.
3. For `Subnet`, choose a public subnet.
4. Set `Auto-assign public IP` to `Enable`.
5. Under `Firewall (security groups)`, choose `Create security group`.
6. Use:
   - `Security group name`: `wikilens-web-sg`
   - `Description`: `WikiLens web and SSH access`
7. Add these inbound rules:
   - `SSH`, source `My IP`
   - `HTTP`, source `Anywhere-IPv4`
   - `HTTPS`, source `Anywhere-IPv4` only if you know you will later terminate TLS on the instance itself

Important:

- do not add a rule for port `8080`
- the backend should stay private behind Nginx

##### Configure Storage

In `Configure storage`:

- set the root volume to at least `30 GiB`
- volume type: `gp3`

This root disk is for Ubuntu, Docker, images, and temporary working space. We will put dump files and cache on a second EBS volume next.

##### Advanced Details

You can leave most of this untouched for the first deployment.

If you already use AWS Systems Manager and want console-based shell access without SSH, attach an instance profile that includes SSM permissions. If not, skip that for now.

#### Step A4: Launch The Instance

1. Review the summary on the right side.
2. Click `Launch instance`.
3. Wait for the success screen, then click `View all instances`.
4. Wait until:
   - `Instance state` becomes `Running`
   - `Status check` becomes `2/2 checks passed`

#### Step A5: Copy The Public Address

On the instance details page, copy one of these:

- `Public IPv4 address`
- or `Public IPv4 DNS`

You will use it for SSH and first-pass verification.

### Phase B: Create And Attach The Persistent EBS Data Volume

You can keep everything on the root volume, but for this project a separate data volume is cleaner because dump files and cache artifacts are large and long-lived.

#### Step B1: Confirm The Instance Availability Zone

1. Stay on the EC2 instance details page.
2. Note the `Availability Zone`, for example `ap-south-1a`.

Your EBS volume must be created in the same Availability Zone as the instance.

#### Step B2: Create The Volume

1. In the EC2 left sidebar, under `Elastic Block Store`, click `Volumes`.
2. Click `Create volume`.
3. Use:
   - `Volume type`: `gp3`
   - `Size`: `80 GiB` to start
   - `Availability Zone`: the same one as your EC2 instance
4. Under tags, add:
   - `Key`: `Name`
   - `Value`: `wikilens-data`
5. Click `Create volume`.

#### Step B3: Attach The Volume

1. Select the new volume.
2. Click `Actions` -> `Attach volume`.
3. For `Instance`, choose `wikilens-prod-1`.
4. For `Device name`, accept the default or use `/dev/sdf`.
5. Click `Attach volume`.

### Phase C: SSH Into The Instance

On your local machine:

```bash
chmod 400 /path/to/wikilens-prod.pem
ssh -i /path/to/wikilens-prod.pem ubuntu@<EC2_PUBLIC_IP>
```

If your key file has a different name, replace it. If AWS gave you a DNS hostname instead of an IP, you can use that instead.

### Phase D: Mount The New EBS Volume

After logging in to Ubuntu, identify the attached disk.

#### Step D1: Find The Device Name

```bash
lsblk
```

On Nitro-based EC2 instances, the volume may appear as something like `/dev/nvme1n1` even if you attached it as `/dev/sdf`.

Check whether the disk already has a filesystem:

```bash
sudo file -s /dev/nvme1n1
```

Replace `/dev/nvme1n1` with the actual device name from `lsblk`.

Interpret the result like this:

- if the output ends with `data`, the volume is empty and you should format it
- if it already shows a filesystem like `ext4` or `xfs`, do not format it

#### Step D2: Format The Volume If It Is Empty

Only do this once, and only if the previous command showed `data`.

```bash
sudo mkfs -t ext4 /dev/nvme1n1
```

#### Step D3: Mount The Volume

```bash
sudo mkdir -p /srv/wikilens
sudo mount /dev/nvme1n1 /srv/wikilens
df -h /srv/wikilens
```

You should see the new volume mounted at `/srv/wikilens`.

#### Step D4: Make The Mount Persistent Across Reboots

Get the device UUID:

```bash
sudo blkid /dev/nvme1n1
```

Copy the UUID value and back up `/etc/fstab`:

```bash
sudo cp /etc/fstab /etc/fstab.orig
```

Open `/etc/fstab`:

```bash
sudo nano /etc/fstab
```

Add a line like this at the end:

```text
UUID=<YOUR_UUID> /srv/wikilens ext4 defaults,nofail 0 2
```

Save and exit, then test it:

```bash
sudo mount -a
df -h /srv/wikilens
```

If `mount -a` returns without errors, your mount configuration is good.

#### Step D5: Create The Runtime Directories

```bash
sudo mkdir -p /srv/wikilens/dumps /srv/wikilens/cache
sudo chown -R ubuntu:ubuntu /srv/wikilens
```

### Phase E: Install Docker And Git

Use the official Docker apt repository flow on Ubuntu 24.04.

#### Step E1: Install Docker

```bash
sudo apt update
sudo apt install -y ca-certificates curl git
sudo install -m 0755 -d /etc/apt/keyrings
sudo curl -fsSL https://download.docker.com/linux/ubuntu/gpg -o /etc/apt/keyrings/docker.asc
sudo chmod a+r /etc/apt/keyrings/docker.asc
```

Create the Docker apt source:

```bash
sudo tee /etc/apt/sources.list.d/docker.sources > /dev/null <<EOF
Types: deb
URIs: https://download.docker.com/linux/ubuntu
Suites: $(. /etc/os-release && echo "${UBUNTU_CODENAME:-$VERSION_CODENAME}")
Components: stable
Signed-By: /etc/apt/keyrings/docker.asc
EOF
```

Install Docker Engine, Buildx, and Compose:

```bash
sudo apt update
sudo apt install -y docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin
```

Verify:

```bash
sudo systemctl status docker --no-pager
sudo docker run hello-world
```

#### Step E2: Allow Your User To Run Docker Without `sudo`

```bash
sudo usermod -aG docker $USER
newgrp docker
docker --version
docker compose version
```

### Phase F: Clone The Repository

Because Crow is a submodule, clone the repo with submodules included.

```bash
cd /opt
sudo git clone --recurse-submodules <YOUR_REPOSITORY_URL> wikilens
sudo chown -R ubuntu:ubuntu /opt/wikilens
cd /opt/wikilens
```

If you already cloned the repo without submodules, fix it like this:

```bash
cd /opt/wikilens
git submodule update --init --recursive
```

### Phase G: Add The Docker Files From Section 4

At this point you are on the EC2 host in `/opt/wikilens`.

Create the files one by one:

```bash
cd /opt/wikilens
nano backend/.dockerignore
nano backend/Dockerfile
nano frontend/.dockerignore
nano frontend/Dockerfile
nano frontend/nginx.conf
nano docker-compose.yml
```

Paste the file contents from Section 4 into each file.

If you are using `nano`:

- paste the text
- press `Ctrl+O`
- press `Enter`
- press `Ctrl+X`

### Phase H: Download The Wikipedia Dump

The current backend supports compressed `.xml.bz2`, so keep the compressed file and save storage space.

```bash
cd /srv/wikilens/dumps
wget https://dumps.wikimedia.org/simplewiki/latest/simplewiki-latest-pages-articles.xml.bz2
ls -lh
```

You should see `simplewiki-latest-pages-articles.xml.bz2` in the directory.

### Phase I: Build And Start The Containers

From the repo root:

```bash
cd /opt/wikilens
docker compose build
docker compose up -d
```

Watch container status:

```bash
docker compose ps
```

Watch backend logs in real time:

```bash
docker compose logs -f backend
```

What you should expect:

- on the first run, the backend will index the dump because the cache directory is empty
- after the cache exists, later restarts should load the cache instead of rebuilding it
- the frontend may come up first, but search will not work until the backend has fully loaded

If you want to also watch the frontend:

```bash
docker compose logs -f frontend
```

### Phase J: Verify The Deployment Over The Public EC2 Address

From inside the EC2 instance:

```bash
curl http://127.0.0.1/
curl http://127.0.0.1/api/stats
curl "http://127.0.0.1/api/search?q=alan+turing&limit=3"
curl "http://127.0.0.1/api/suggest?q=comp&limit=5"
```

From your local browser:

```text
http://<EC2_PUBLIC_IP>/
```

If the page loads but search fails, the most likely cause is that the backend is still indexing or loading cache. Check:

```bash
docker compose logs -f backend
```

### Phase K: Optional One-Time Offline PageRank Recompute

The backend can recompute PageRank from the persisted link graph after the initial cache is built.

Run:

```bash
cd /opt/wikilens
docker compose run --rm backend ./wikilens --recompute-pagerank
docker compose restart backend
```

Use this after the first full indexing pass completes.

## 6. Optional But Recommended: Add ALB, ACM, And Route 53

Get the raw EC2 deployment working first. After that, add AWS-native HTTPS and DNS.

### Phase L: Request The ACM Certificate

If you own a domain and want HTTPS with an ALB, request the certificate first.

#### Step L1: Open ACM

1. In the AWS Console search bar, type `Certificate Manager` or `ACM`.
2. Open `AWS Certificate Manager`.
3. Make sure you are still in the same AWS Region where your ALB will live.

#### Step L2: Request The Certificate

1. Click `Request a certificate`.
2. Choose `Request a public certificate`.
3. Click `Next`.
4. In `Domain names`, enter your hostname.

Common options:

- `search.example.com` if this app is only one subdomain
- `example.com` and `www.example.com` if you want the root and `www`

5. Validation method: choose `DNS validation`.
6. Key algorithm: keep `RSA 2048` unless you have a reason to use another one.
7. Click `Request`.

If your DNS is already in Route 53 in the same account, AWS can often help you create the validation records from the certificate page.

Wait until the certificate status changes from `Pending validation` to `Issued`.

### Phase M: Create The Target Group

The target group represents your EC2 instance on port `80`.

#### Step M1: Open Target Groups

1. Go back to `EC2`.
2. In the left sidebar under `Load Balancing`, click `Target Groups`.
3. Click `Create target group`.

#### Step M2: Fill In The Target Group Form

Use:

- `Choose a target type`: `Instances`
- `Target group name`: `wikilens-frontend-tg`
- `Protocol`: `HTTP`
- `Port`: `80`
- `VPC`: the same VPC as your EC2 instance
- `Health check protocol`: `HTTP`
- `Health check path`: `/api/stats`

Why `/api/stats`:

- it checks the public frontend path
- it confirms Nginx proxying works
- it confirms the backend is answering

Click `Next`.

#### Step M3: Register The Instance

1. In `Available instances`, select `wikilens-prod-1`.
2. Click `Include as pending below`.
3. Click `Create target group`.

### Phase N: Create The Application Load Balancer

#### Step N1: Open The Load Balancer Wizard

1. In `EC2`, under `Load Balancing`, click `Load Balancers`.
2. Click `Create load balancer`.
3. Under `Application Load Balancer`, click `Create`.

#### Step N2: Fill In The ALB Form

Use:

- `Load balancer name`: `wikilens-alb`
- `Scheme`: `Internet-facing`
- `IP address type`: `IPv4`

In `Network mapping`:

- choose the same VPC
- select at least two public subnets in different Availability Zones

In `Security groups`:

- either choose an existing web-facing security group or create one that allows:
  - `HTTP 80` from `0.0.0.0/0`
  - `HTTPS 443` from `0.0.0.0/0`

In `Listeners and routing`:

- for the `HTTP:80` listener, forward to `wikilens-frontend-tg`

Click `Create load balancer`.

#### Step N3: Add HTTPS

After the ALB exists and the ACM certificate is `Issued`:

1. Open the ALB.
2. Go to the `Listeners and rules` tab.
3. Click `Add listener`.
4. Choose:
   - `Protocol`: `HTTPS`
   - `Port`: `443`
5. Select the ACM certificate you requested.
6. Forward the listener to `wikilens-frontend-tg`.
7. Save.

Optional improvement:

- edit the `HTTP:80` listener and change the default action to redirect to `HTTPS:443`

### Phase O: Point Route 53 At The ALB

Do this only if your DNS is hosted in Route 53.

#### Step O1: Open The Hosted Zone

1. Open `Route 53`.
2. Click `Hosted zones`.
3. Select your domain.

#### Step O2: Create The Alias Record

1. Click `Create record`.
2. Use:
   - `Record name`: leave blank for the root domain, or enter something like `search`
   - `Record type`: `A`
   - `Alias`: `On`
3. For `Route traffic to`, choose:
   - `Alias to Application and Classic Load Balancer`
4. Choose the correct region.
5. Choose the ALB DNS name from the dropdown.
6. Click `Create records`.

After DNS propagates, browse to your domain instead of the raw EC2 IP.

## 7. Operations After Deployment

### Daily Commands You Will Actually Use

Rebuild after code changes:

```bash
cd /opt/wikilens
git pull --recurse-submodules
docker compose up -d --build
```

Restart containers:

```bash
cd /opt/wikilens
docker compose restart
```

Stop containers:

```bash
cd /opt/wikilens
docker compose down
```

Watch backend logs:

```bash
cd /opt/wikilens
docker compose logs -f backend
```

Watch frontend logs:

```bash
cd /opt/wikilens
docker compose logs -f frontend
```

Check whether the app is responding:

```bash
curl http://127.0.0.1/api/stats
```

### Back Up The Persistent Data

At minimum, back up:

- `/srv/wikilens/dumps`
- `/srv/wikilens/cache`

The cleanest AWS-native option is EBS snapshots of the mounted `/srv/wikilens` volume.

### When To Clear Cache

Clear `/srv/wikilens/cache` only when one of these is true:

- indexing logic changed
- tokenizer or Wikitext cleaning changed
- PageRank persistence behavior changed
- you switched to a different dump file and want a fresh rebuild

Example:

```bash
rm -f /srv/wikilens/cache/*
cd /opt/wikilens
docker compose restart backend
```

Do this carefully. The next backend boot will rebuild from the dump.

### One Important Networking Note

Do not rely on Ubuntu `ufw` alone to protect Docker-published ports. The safer pattern for this project is:

- do not publish backend port `8080` in Compose
- publish only frontend port `80`
- use AWS Security Groups as the main network boundary

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

## 11. Official Reference Links

These are the main vendor docs I used to make the console steps above concrete.

- EC2 launch wizard: https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/ec2-launch-instance-wizard.html
- EC2 security groups: https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/creating-security-group.html
- EBS volume creation: https://docs.aws.amazon.com/ebs/latest/userguide/ebs-creating-volume.html
- EBS mount and `/etc/fstab` guidance: https://docs.aws.amazon.com/ebs/latest/userguide/ebs-using-volumes.html
- ACM public certificates: https://docs.aws.amazon.com/acm/latest/userguide/acm-public-certificates.html
- ALB target groups: https://docs.aws.amazon.com/elasticloadbalancing/latest/application/create-target-group.html
- Application Load Balancer creation: https://docs.aws.amazon.com/elasticloadbalancing/latest/application/create-application-load-balancer.html
- HTTPS listener on ALB: https://docs.aws.amazon.com/elasticloadbalancing/latest/application/create-https-listener.html
- Route 53 alias to ALB: https://docs.aws.amazon.com/Route53/latest/DeveloperGuide/routing-to-elb-load-balancer.html
- Docker Engine on Ubuntu: https://docs.docker.com/engine/install/ubuntu/
