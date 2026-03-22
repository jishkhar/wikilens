# Quick Restart Guide: After Stopping Instance and Deleting EBS Volume

If you have **stopped** (not terminated) your EC2 instance and deleted the EBS volume, use this guide to redeploy WikiLens. You can reuse your existing instance. Most steps reference the main [deployment.md](deployment.md) guide.

## Overview

Your EC2 instance still exists and can be reused. Since you deleted the persistent storage, you'll need to:
1. Start the existing EC2 instance
2. Create and attach a new EBS volume
3. Mount the volume and create directories
4. Re-download the Wikipedia dump (or use a compressed version)
5. Rebuild and start containers

Estimated time: 15 minutes setup + 1-4 hours for first indexing (depending on dump size and instance speed).

## Quick Steps

### 1. Start Your Existing EC2 Instance

Go to **EC2 Console**:
1. Select your `wikilens-prod-1` instance
2. Click **Instance State** → **Start**
3. Wait for state to be `Running` and status checks to show `2/2 checks passed`
4. Copy the public IP address for SSH

Your existing instance, root volume, Docker installation, and repo clone are all still there.

### 2. Create and Attach EBS Volume

Follow **Section 5, Phase B** in [deployment.md](deployment.md):
- Create 80 GiB gp3 volume in the same Availability Zone as your instance
- Attach it to the instance
- Tag it as `wikilens-data`

### 3. SSH and Mount the Volume

Follow **Section 5, Phase D** in [deployment.md](deployment.md):
```bash
chmod 400 /path/to/wikilens-prod.pem
ssh -i /path/to/wikilens-prod.pem ubuntu@<EC2_PUBLIC_IP>
```

Then on the instance:
```bash
lsblk
sudo file -s /dev/nvme1n1  # adjust device name if needed
sudo mkfs -t ext4 /dev/nvme1n1
sudo mkdir -p /srv/wikilens
sudo mount /dev/nvme1n1 /srv/wikilens
```

Make it persistent:
```bash
sudo blkid /dev/nvme1n1
sudo cp /etc/fstab /etc/fstab.orig
sudo nano /etc/fstab
# Add: UUID=<YOUR_UUID> /srv/wikilens ext4 defaults,nofail 0 2
sudo mount -a
```

Create directories:
```bash
sudo mkdir -p /srv/wikilens/dumps /srv/wikilens/cache
sudo chown -R ubuntu:ubuntu /srv/wikilens
```

### 4. Verify Repo and Docker Files

Docker and the repo should already be present from your original deployment. Verify:
```bash
cd /opt/wikilens
docker --version
docker compose version
```

Verify Docker files are still there:
```bash
ls -la backend/.dockerignore backend/Dockerfile
ls -la frontend/.dockerignore frontend/Dockerfile frontend/nginx.conf
ls -la docker-compose.yml
```

If any are missing, refer to **Section 4** of [deployment.md](deployment.md) to create them.

### 5. Download the Wikipedia Dump

You have two options:

**Option A: Keep compressed (saves storage)**
```bash
cd /srv/wikilens/dumps
wget https://dumps.wikimedia.org/simplewiki/latest/simplewiki-latest-pages-articles.xml.bz2
ls -lh
```

**Option B: Download and decompress**
```bash
cd /srv/wikilens/dumps
wget https://dumps.wikimedia.org/simplewiki/latest/simplewiki-latest-pages-articles.xml.bz2
bunzip2 simplewiki-latest-pages-articles.xml.bz2
ls -lh
```

For full English Wikipedia, replace `simplewiki` with `enwiki` in the URL (be prepared for ~20GB+ compressed).

### 6. Build and Start Containers

From the repo root:
```bash
cd /opt/wikilens
docker compose build
docker compose up -d
```

Watch the backend indexing:
```bash
docker compose logs -f backend
```

Expected output:
- First run: Backend will parse the dump and build cache (can take 30 min to several hours)
- Subsequent runs: Backend will load from cache (< 1 minute)

### 7. Verify Deployment

Once backend finishes loading:
```bash
curl http://127.0.0.1/
curl http://127.0.0.1/api/stats
curl "http://127.0.0.1/api/search?q=alan+turing&limit=3"
```

From your browser:
```
http://<EC2_PUBLIC_IP>/
```

## Optional: Add ALB and HTTPS Later

After basic deployment is working, follow **Section 6** in [deployment.md](deployment.md) to add:
- AWS Certificate Manager (ACM) certificate
- Application Load Balancer (ALB)
- Route 53 DNS (if applicable)

## Key Differences From Original Deployment

- **EBS Volume**: New volume created, so no previous cache exists. First startup will be slow (initial indexing).
- **Dump File**: You must re-download it. It's no longer on the deleted volume.
- **Cache**: Will be rebuilt from scratch on first run.
- **EC2 Instance**: Reused from original deployment (all setup already in place).
- **Docker & Repo**: Already installed and cloned (no reinstall needed).

## Troubleshooting

**Docker build fails:**
- Ensure git submodules are initialized: `git submodule update --init --recursive`
- Check disk space: `df -h`

**Backend takes too long to index:**
- This is normal for first run. Check logs: `docker compose logs -f backend`
- Consider instance size if it takes > 4 hours for Simple English

**Frontend loads but search doesn't work:**
- Backend is still indexing. Wait and check logs: `docker compose logs -f backend`

**"No space left on device" errors:**
- Increase EBS volume size or clean up: `docker system prune`

## Next Steps

- Read **Section 7** (Operations After Deployment) in [deployment.md](deployment.md) for daily operational commands
- Read **Section 8** for deployment constraints and risks
- Read **Section 9** for recommended future improvements (especially systemd auto-start)

## When You Need To Stop Again

To save costs, stop (not terminate) your instance and its associated resources:

```bash
# On the instance, stop containers (optional but clean):
cd /opt/wikilens
docker compose down

# In AWS Console:
# 1. EC2 → Instances → Select instance → Instance State → Stop
# 2. EC2 → Volumes → Select wikilens-data volume → Create snapshot (optional backup)
```

When ready to restart, follow the "Restarting Your Deployment After A Stop" section in [deployment.md](deployment.md#restarting-your-deployment-after-a-stop).
