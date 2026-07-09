# Dereth game server — dependency-free Python 3 stdlib, so the image is tiny.
# Build (from the repo root):   docker build -t dereth-server -f deploy/Dockerfile .
# Run:                          docker run -d --name dereth -p 127.0.0.1:8787:8787 \
#                                 -v dereth-data:/data dereth-server
# Accounts/characters persist in the named volume (SQLite at /data/dereth.db).
# Front it with the nginx config in this directory for static files + TLS + /ws (see DEPLOY.md);
# the container binds 0.0.0.0 INSIDE the container but the -p above keeps it loopback-only on
# the host, matching the systemd deployment's posture.
FROM python:3.12-slim

RUN useradd --system --home /app dereth
WORKDIR /app
# The server reads admin_kilmer.json (beside it) and ../assets/{acrewards,acitems,acspellstats,achousing}.json
# at boot; without them a Docker-deployed server silently loses the retail loot/spell catalog, the seeded
# Admin character, and (achousing) the authentic dwelling-plot set used to validate house claims (#451).
# Ship them so the container matches the systemd (whole-repo) deployment.
COPY server/dereth_server.py server/admin_kilmer.json ./
COPY assets/acrewards.json assets/acitems.json assets/acspellstats.json assets/achousing.json /assets/

ENV DERETH_HOST=0.0.0.0 \
    DERETH_PORT=8787 \
    DERETH_DB=/data/dereth.db

RUN mkdir -p /data && chown dereth:dereth /data
VOLUME /data
USER dereth
EXPOSE 8787

CMD ["python", "dereth_server.py"]
