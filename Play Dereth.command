#!/bin/bash
# Double-click in Finder to play Dereth. Serves the game from a tiny LOCAL web server
# (fully offline — no internet needed) so the browser can load three.min.js reliably.
# Opening index.html directly as a file:// page makes some browsers block three.min.js;
# serving over http://localhost avoids that entirely.
cd "$(dirname "$0")"

PORT=8777
# pick a free port if 8777 is taken
while lsof -i :"$PORT" >/dev/null 2>&1; do PORT=$((PORT+1)); done

# start the local server in the background
python3 -m http.server "$PORT" >/dev/null 2>&1 &
SERVER_PID=$!

# give it a moment, then open the browser to the served page
sleep 1
open "http://localhost:$PORT/index.html"

echo "Dereth is running at http://localhost:$PORT/index.html"
echo "Keep this window open while you play. Close it (or press Ctrl+C) to stop the game."

# stop the server when this window is closed / interrupted
trap 'kill $SERVER_PID 2>/dev/null' EXIT
wait $SERVER_PID
