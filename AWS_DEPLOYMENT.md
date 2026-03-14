# WikiLens AWS Deployment Guide (Docker Containerization)

This guide deploys WikiLens to AWS using Docker on a single EC2 instance.

## Stack

- `backend` container: C++ Crow service on port `8080`
- `frontend` container: Nginx serving React build on port `80`
- Frontend proxies `/api/*` to backend container internally
- Host bind mounts keep dump + cache persistent across container restarts

---

## 0) Deployment Architecture

Browser → `frontend` container (`:80`, Nginx)

- `/` serves static React app
- `/api/*` proxies to `backend:8080`

Persistent host storage:

- `backend/simplewiki-latest-pages-articles.xml.1` (dump file)
- `backend/data/cache/*.bin` (index cache)

---

## 1) Launch AWS EC2

1. Open AWS Console → EC2 → Launch instance.
2. Recommended:
    - AMI: Ubuntu Server 24.04 LTS
    - Instance: `t3.medium` minimum (larger is better for indexing)
    - Disk: 50 GB or more
3. Security Group inbound:
    - `22` from your IP
    - `80` from anywhere
    - `443` from anywhere (optional, if TLS)
4. Attach key pair and launch.

---

## 2) Connect and Install Docker

SSH:

```bash
ssh -i /path/to/key.pem ubuntu@<EC2_PUBLIC_IP>
```

Install Docker Engine + Compose plugin:

```bash
sudo apt update
sudo apt install -y ca-certificates curl gnupg
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
sudo apt install -y docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin git
sudo usermod -aG docker $USER
newgrp docker
docker --version
docker compose version
```

---

## 3) Clone Project

```bash
cd /opt
sudo git clone https://github.com/jishkhar/wikilens.git
sudo chown -R $USER:$USER /opt/wikilens
cd /opt/wikilens
```

---

## 4) Create Docker Files

### 4.1 Backend Dockerfile

Create `backend/Dockerfile`:

```dockerfile
FROM ubuntu:24.04 AS build
RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential cmake libexpat1-dev libasio-dev && \
      rm -rf /var/lib/apt/lists/*

WORKDIR /app/backend
COPY . .
RUN cmake -S . -B build && cmake --build build -j"$(nproc)"

FROM ubuntu:24.04 AS runtime
RUN apt-get update && apt-get install -y --no-install-recommends \
      libexpat1 libasio-dev && \
      rm -rf /var/lib/apt/lists/*

WORKDIR /app/backend/build
COPY --from=build /app/backend/build/wikilens /app/backend/build/wikilens
COPY --from=build /app/backend/data /app/backend/data

EXPOSE 8080
CMD ["/app/backend/build/wikilens"]
```

### 4.2 Frontend Dockerfile

Create `frontend/Dockerfile`:

```dockerfile
FROM node:20-alpine AS build
WORKDIR /app
COPY package*.json ./
RUN npm ci
COPY . .
RUN npm run build

FROM nginx:1.27-alpine
COPY nginx.conf /etc/nginx/conf.d/default.conf
COPY --from=build /app/dist /usr/share/nginx/html
EXPOSE 80
CMD ["nginx", "-g", "daemon off;"]
```

### 4.3 Frontend Nginx Config

Create `frontend/nginx.conf`:

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

### 4.4 Docker Compose

Create `/opt/wikilens/docker-compose.yml`:

```yaml
services:
   backend:
      build:
         context: ./backend
         dockerfile: Dockerfile
      container_name: wikilens-backend
      restart: unless-stopped
      volumes:
         - ./backend/data/cache:/app/backend/data/cache
         - ./backend/simplewiki-latest-pages-articles.xml.1:/app/backend/simplewiki-latest-pages-articles.xml.1:ro
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

---

## 5) Update Frontend API URL (Required)

Your current app uses `http://localhost:8080/search...`.

In production, this breaks. Change the request URL in `frontend/src/App.tsx` to:

```ts
`/api/search?q=${encodeURIComponent(q)}&offset=${off}&limit=${PAGE_SIZE}`
```

---

## 6) Add Dump File

Put the dump at:

`/opt/wikilens/backend/simplewiki-latest-pages-articles.xml.1`

Example:

```bash
cd /opt/wikilens/backend
wget https://dumps.wikimedia.org/simplewiki/latest/simplewiki-latest-pages-articles.xml.bz2
bzip2 -d simplewiki-latest-pages-articles.xml.bz2
mv simplewiki-latest-pages-articles.xml simplewiki-latest-pages-articles.xml.1
```

---

## 7) Build and Start Containers

```bash
cd /opt/wikilens
docker compose up -d --build
```

Check status:

```bash
docker compose ps
docker compose logs -f backend
docker compose logs -f frontend
```

First backend start will index and write cache; it can take time.

---

## 8) Verify Deployment

Backend health (from host):

```bash
curl http://127.0.0.1/api/search?q=computer&limit=1
```

Browser:

`http://<EC2_PUBLIC_IP>/`

---

## 9) Persistency and Cache Behavior

Because of bind mounts, these survive container restarts:

- `backend/simplewiki-latest-pages-articles.xml.1`
- `backend/data/cache/inverted_index.bin`
- `backend/data/cache/document_store.bin`

If you change indexing/ranking logic or dump file, clear cache and restart:

```bash
rm -f /opt/wikilens/backend/data/cache/inverted_index.bin \
         /opt/wikilens/backend/data/cache/document_store.bin
cd /opt/wikilens
docker compose restart backend
```

---

## 10) Run Containers on Boot

`restart: unless-stopped` handles daemon restarts, but ensure Docker starts at boot:

```bash
sudo systemctl enable docker
```

Optional: add a systemd unit to run `docker compose up -d` on boot for this project.

---

## 11) Optional HTTPS

Two common options:

1. Put AWS ALB + ACM cert in front of EC2 (recommended for AWS-native TLS).
2. Add a certbot/Nginx reverse proxy on host in front of container port 80.

---

## 12) Useful Commands

Rebuild after code changes:

```bash
cd /opt/wikilens
docker compose up -d --build
```

Stop:

```bash
docker compose down
```

Tail logs:

```bash
docker compose logs -f
```

---

## Common Issues

1. Frontend loads but search fails
    - API URL in frontend was not switched to `/api/search...`.

2. Backend exits immediately
    - Dump file missing at `backend/simplewiki-latest-pages-articles.xml.1`.

3. Very slow first run
    - Expected; initial indexing + PageRank generation.

4. Cache not reused
    - Ensure `backend/data/cache` bind mount exists and is writable.

---

## Next AWS Step (Production Scale)

After EC2 Docker validation, move to:

- ECR for image registry
- ECS/Fargate for orchestration
- ALB for traffic routing
- CloudFront + S3 for frontend static hosting
