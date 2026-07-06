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
COPY server/dereth_server.py ./

ENV DERETH_HOST=0.0.0.0 \
    DERETH_PORT=8787 \
    DERETH_DB=/data/dereth.db

RUN mkdir -p /data && chown dereth:dereth /data
VOLUME /data
USER dereth
EXPOSE 8787

CMD ["python", "dereth_server.py"]
