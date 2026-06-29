# Deploying Dereth to a DigitalOcean droplet (Ubuntu 24.04)

The server is dependency-free Python 3 — Ubuntu 24.04 already ships Python 3.12, so there's
nothing to `pip install`. We run it under **systemd** bound to localhost, and front it with
**nginx** for static files + TLS + the `/ws` WebSocket proxy.

```
Browser ──https/wss──> nginx (443, TLS) ──┬── static index.html + three.min.js  (/)
                                          └── proxy /ws ──> 127.0.0.1:8787 (python game server, systemd)
```

## 0. Prerequisites
- A droplet running Ubuntu 24.04 (1 vCPU / 1 GB is plenty to start).
- A domain (e.g. `dereth.example.com`) with an **A record** pointing at the droplet's IP.

## 1. Base packages
```bash
ssh root@YOUR_DROPLET_IP
apt update && apt upgrade -y
apt install -y nginx certbot python3-certbot-nginx git
python3 --version           # expect 3.12.x — no venv/pip needed
```

## 2. Service user + data dir
```bash
useradd --system --home /opt/dereth --shell /usr/sbin/nologin dereth || true
mkdir -p /opt/dereth /var/lib/dereth
chown -R dereth:dereth /var/lib/dereth
```

## 3. Get the code
```bash
git clone https://github.com/w5ohr/Dereth.git /opt/dereth
chown -R dereth:dereth /opt/dereth
```

## 4. Run the game server (systemd)
```bash
cp /opt/dereth/deploy/dereth.service /etc/systemd/system/dereth.service
systemctl daemon-reload
systemctl enable --now dereth
systemctl status dereth --no-pager        # should be active (running)
journalctl -u dereth -n 20 --no-pager     # "Dereth server listening on ('127.0.0.1', 8787)"
```

## 5. nginx site (static + /ws proxy)
```bash
cp /opt/dereth/deploy/nginx-dereth.conf /etc/nginx/sites-available/dereth
sed -i 's/dereth.example.com/YOUR_DOMAIN/' /etc/nginx/sites-available/dereth
ln -sf /etc/nginx/sites-available/dereth /etc/nginx/sites-enabled/dereth
rm -f /etc/nginx/sites-enabled/default
nginx -t && systemctl reload nginx
```

## 6. TLS (Let's Encrypt, auto-renewing)
```bash
certbot --nginx -d YOUR_DOMAIN        # adds the 443 block + HTTP->HTTPS redirect
```
The client auto-selects `wss://YOUR_DOMAIN/ws` when served over https — matching the nginx proxy.

## 7. Firewall
```bash
ufw allow OpenSSH
ufw allow 'Nginx Full'                # 80 + 443; the game port 8787 stays internal
ufw --force enable
```

Visit `https://YOUR_DOMAIN` → Register → you're in the shared world.

## Updating after new commits
```bash
cd /opt/dereth && git pull
systemctl restart dereth              # picks up server changes
systemctl reload nginx                # static client is served straight from the repo
```
(or run `deploy/update.sh` on the droplet.)

## Backups
Player accounts + characters live in **`/var/lib/dereth/dereth.db`** (SQLite). Back it up:
```bash
sqlite3 /var/lib/dereth/dereth.db ".backup '/var/backups/dereth-$(date +%F).db'"
```

## Scaling notes (later)
- The server is a single asyncio process (one core). For more players, run multiple instances
  behind nginx with sticky routing + a shared DB, or shard by zone. Move SQLite → Postgres when
  write contention shows up. None of this is needed to launch with friends.
