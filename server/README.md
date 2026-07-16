# Dereth MMO Server

Authoritative game server for Dereth. **Dependency-free** — Python 3 standard library only
(no `pip install`). WebSocket transport is hand-rolled over `asyncio`; accounts and persistent
characters live in SQLite.

## Run

```bash
python3 server/dereth_server.py
# listens on 0.0.0.0:8787 by default
```

Environment overrides:

| var | default | meaning |
|-----|---------|---------|
| `DERETH_HOST` | `0.0.0.0` | bind address |
| `DERETH_PORT` | `8787` | bind port |
| `DERETH_DB` | `server/dereth.db` | SQLite path (use a persistent volume in the cloud) |
| `DERETH_GITHUB_TOKEN` | *(unset)* | fine-grained GitHub token with **Issues: write** on the repo below — enables the in-game `/bug` reporter to file issues. Unset → clients fall back to a pre-filled github.com new-issue page. Never commit this token (see #440/#459). |
| `DERETH_GITHUB_REPO` | `w5ohr/Dereth` | repo that receives `player-report` issues |
| `DERETH_BUG_COOLDOWN` | `120` | seconds between accepted reports per account |
| `DERETH_BUG_DAILY_MAX` | `10` | accepted reports per account per UTC day |

## Test

With the server running:

```bash
python3 server/test_client.py [host] [port]
```

Spins up two WebSocket clients and asserts auth, session tokens, character persistence,
chat relay, presence (join/leave), and world snapshots. Exits non-zero on any failure.

## Protocol (JSON over WebSocket)

Client → server: `register` / `login` / `resume` (auth), then `input` (position/state),
`chat`, `save` (persist character blob), `ping`.

Server → client: `auth_ok` (id, token, stored character) / `auth_err`, `snapshot`
(10 Hz list of all players), `chat`, `system` (join/leave notices), `pong`.

## Status

- **M1 (done):** accounts (scrypt), persistence, chat, presence, 10 Hz snapshots.
- **M3 (planned):** monsters / combat / world events become server-authoritative.
- **M4 (planned):** Dockerfile + cloud deploy + `wss://` TLS termination.

The browser client falls back to offline solo play when no server is reachable.
