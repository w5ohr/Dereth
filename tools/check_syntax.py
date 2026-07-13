#!/usr/bin/env python3
"""Parse-only JS syntax gate for the client. Extracts every inline <script> (without src) from
index.html and wraps each (plus sw.js) so it PARSES without executing — no THREE/DOM/self needed — then
checks with `node --check` if available, else JavaScriptCore's `jsc`. Exit 0 if everything parses,
nonzero on the first syntax error. Used by CI and runnable locally (`python3 tools/check_syntax.py`)."""
import os, re, shutil, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
JSC = "/System/Library/Frameworks/JavaScriptCore.framework/Versions/Current/Helpers/jsc"

def pick_checker():
    if shutil.which("node"):
        return "node", lambda f: subprocess.run(["node", "--check", f],
                                                 capture_output=True, text=True)
    if os.path.exists(JSC):
        # load() parses + defines the wrapped (uncalled) function → parse-only, no runtime errors
        return "jsc", lambda f: subprocess.run([JSC, "-e", f"load('{f}')"],
                                               capture_output=True, text=True)
    print("ERROR: no JS syntax checker found (need `node` or macOS `jsc`)")
    sys.exit(2)

def main():
    name, check = pick_checker()
    print(f"JS syntax gate via {name}")
    fails = 0
    with tempfile.TemporaryDirectory() as td:
        html = open(os.path.join(ROOT, "index.html"), encoding="utf-8").read()
        scripts = re.findall(r'<script(?![^>]*\bsrc=)[^>]*>(.*?)</script>', html, re.S)
        targets = []
        for i, s in enumerate(scripts):
            p = os.path.join(td, f"inline_{i}.js")
            open(p, "w", encoding="utf-8").write("(function(){\n" + s + "\n})")
            targets.append((f"index.html inline <script> #{i} ({len(s)} chars)", p))
        sw = os.path.join(ROOT, "sw.js")
        if os.path.exists(sw):
            p = os.path.join(td, "sw_wrapped.js")
            open(p, "w", encoding="utf-8").write("(function(){\n" + open(sw, encoding="utf-8").read() + "\n})")
            targets.append(("sw.js", p))
        for label, path in targets:
            r = check(path)
            ok = r.returncode == 0
            print(f"  {'OK  ' if ok else 'FAIL'} {label}")
            if not ok:
                fails += 1
                sys.stderr.write((r.stderr or r.stdout or "").strip() + "\n")
    if fails:
        print(f"\n{fails} syntax error(s)")
        sys.exit(1)
    print("all scripts parse cleanly")

if __name__ == "__main__":
    main()
