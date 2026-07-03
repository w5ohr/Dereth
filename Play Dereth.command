#!/bin/bash
# Double-click in Finder to play Dereth. Starts BOTH local processes the game needs:
#   1. a tiny static web server for the game page (fully offline — no internet needed;
#      opening index.html as file:// makes some browsers block three.min.js)
#   2. the Dereth game server (server/dereth_server.py, WebSocket on :8787) —
#      accounts, characters and the shared world for "Play Online"
cd "$(dirname "$0")"

PORT=8777
# pick a free port if 8777 is taken
while lsof -i :"$PORT" >/dev/null 2>&1; do PORT=$((PORT+1)); done

# start the static server in the background
python3 -m http.server "$PORT" >/dev/null 2>&1 &
WEB_PID=$!

# start the game server too (skip if one is already listening on 8787)
GAME_PID=""
if ! lsof -i :8787 >/dev/null 2>&1; then
  (cd server && python3 dereth_server.py >/dev/null 2>&1) &
  GAME_PID=$!
fi

# give them a moment, then open the browser to the served page
sleep 1
open "http://localhost:$PORT/index.html"

echo "Dereth is running at http://localhost:$PORT/index.html"
if [ -n "$GAME_PID" ]; then echo "Game server (online play) running on ws://127.0.0.1:8787"
else echo "Game server already running on ws://127.0.0.1:8787"; fi
echo "Keep this window open while you play. Close it (or press Ctrl+C) to stop the game."

# stop both servers when this window is closed / interrupted
trap 'kill $WEB_PID $GAME_PID 2>/dev/null' EXIT
wait $WEB_PID
