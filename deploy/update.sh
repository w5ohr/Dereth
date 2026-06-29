#!/usr/bin/env bash
# Pull the latest Dereth and restart the service. Run on the droplet as root.
set -euo pipefail
cd /opt/dereth
git pull --ff-only
chown -R dereth:dereth /opt/dereth
systemctl restart dereth
systemctl reload nginx
echo "updated; server status:"
systemctl status dereth --no-pager | head -5
