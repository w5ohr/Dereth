#!/bin/bash
# Runs every `Open WebUI --<name>test` self-test found in src/main.mm, each in
# a throwaway HOME with the keychain off and a time limit, and prints a table.
# Exit status is non-zero when any test fails, hangs or crashes.
#
#   scripts/selftests.sh [path/to/Open WebUI] [--skip name,name] [--only name,name] [--timeout seconds]
#
# On GitHub Actions the table is also written to the job summary.
set -u
cd "$(dirname "$0")/.."
APP="build/Open WebUI.app/Contents/MacOS/Open WebUI"
SKIP=""
ONLY=""
LIMIT=90
while [ $# -gt 0 ]; do
  case "$1" in
    --skip) SKIP=",$2,"; shift 2 ;;
    --only) ONLY=",$2,"; shift 2 ;;
    --timeout) LIMIT="$2"; shift 2 ;;
    *) APP="$1"; shift ;;
  esac
done
if [ ! -x "$APP" ]; then
  echo "no app binary at $APP - build first (cmake -S . -B build && cmake --build build)" >&2
  exit 2
fi
LOGS="${SELFTEST_LOGS:-build/selftest-logs}"
# Throwaway homes live under build/, not the system temp folder: the script
# sandbox allows writes to the temp folder by design, so a HOME there would
# make "writes outside the working folder are denied" look allowed.
HOMES="$PWD/build/selftest-homes"
rm -rf "$HOMES"
mkdir -p "$HOMES"
rm -rf "$LOGS"
mkdir -p "$LOGS"

FLAGS=$(grep -o '"--[a-z0-9-]*test"' src/main.mm | tr -d '"' | sed 's/^--//' | sort -u)
# --selftest is the integration suite: it needs a live Open WebUI (OWUI_SERVER,
# OWUI_TOKEN or OWUI_EMAIL + OWUI_PASSWORD). Without one it is skipped, and a
# regression check runs instead: against an unreachable server it must fail
# cleanly with exit 1 - it once aborted at exit (SIGABRT) because the worker
# pool's threads were still joinable when static destructors ran.
if [ -z "${OWUI_SERVER:-}" ]; then
  SKIP="${SKIP:-,},selftest,"
  home=$(mktemp -d "$HOMES/home.XXXXXX")
  HOME="$home" OWUI_NO_KEYCHAIN=1 OWUI_SERVER=http://127.0.0.1:9 OWUI_EMAIL=x OWUI_PASSWORD=x "$APP" --selftest > "$LOGS/selftest-offline.log" 2>&1
  code=$?
  rm -rf "$home"
  if [ "$code" -eq 1 ]; then
    echo "selftest-offline ok         exits 1 cleanly without a server"
    offline_ok=1
  else
    echo "selftest-offline FAIL       exit $code without a server (134 = abort at exit)"
    offline_ok=0
  fi
fi
total=0; passed=0; failed=0; skipped=0
rows=""
for name in $FLAGS; do
  if [ -n "$ONLY" ] && [[ "$ONLY" != *",$name,"* ]]; then continue; fi
  total=$((total + 1))
  if [[ "$SKIP" == *",$name,"* ]]; then
    skipped=$((skipped + 1))
    why=""
    [ "$name" = "selftest" ] && why="needs a live Open WebUI server (set OWUI_SERVER)"
    rows+="| $name | skipped | $why | |"$'\n'
    printf "%-16s skipped    %s\n" "$name" "$why"
    continue
  fi
  home=$(mktemp -d "$HOMES/home.XXXXXX")
  log="$LOGS/$name.log"
  start=$(date +%s)
  HOME="$home" OWUI_NO_KEYCHAIN=1 "$APP" "--$name" > "$log" 2>&1 &
  pid=$!
  ( sleep "$LIMIT"; kill -9 "$pid" 2>/dev/null ) &
  watchdog=$!
  wait "$pid"
  code=$?
  kill "$watchdog" 2>/dev/null
  wait "$watchdog" 2>/dev/null
  secs=$(( $(date +%s) - start ))
  rm -rf "$home"
  summary=$(grep -E "^[0-9]+ passed, [0-9]+ failed" "$log" | tail -1)
  if [ "$code" -eq 137 ]; then
    status="TIMEOUT"
  elif [ "$code" -ne 0 ]; then
    status="FAIL"
  elif [ -z "$summary" ]; then
    status="NO SUMMARY"
  else
    status="ok"
  fi
  if [ "$status" = "ok" ]; then passed=$((passed + 1)); else failed=$((failed + 1)); fi
  rows+="| $name | $status | ${summary:-exit $code} | ${secs}s |"$'\n'
  printf "%-16s %-10s %-24s %ss\n" "$name" "$status" "${summary:-exit $code}" "$secs"
  if [ "$status" != "ok" ]; then
    grep -E "^  FAIL" "$log" | head -5 | sed 's/^/                 /'
    [ "$status" != "FAIL" ] && tail -3 "$log" | sed 's/^/                 /'
  fi
done

if [ "${offline_ok:-1}" -eq 0 ]; then failed=$((failed + 1)); rows+="| selftest-offline | FAIL | exit without a server was not 1 | |"$'\n'; fi
if [ -n "${offline_ok:-}" ] && [ "$offline_ok" -eq 1 ]; then rows+="| selftest-offline | ok | exits 1 cleanly without a server | |"$'\n'; fi
rm -rf "$HOMES"
line="$passed of $((total - skipped)) self-tests passed ($failed failed, $skipped skipped)"
echo
echo "$line"
if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
  {
    echo "### Open WebUI desktop self-tests"
    echo
    echo "$line"
    echo
    echo "| test | result | checks | time |"
    echo "|---|---|---|---|"
    printf "%s" "$rows"
  } >> "$GITHUB_STEP_SUMMARY"
fi
[ "$failed" -eq 0 ]
