#!/usr/bin/env python3
"""Parse-only JS syntax gate for the client. Extracts every inline <script> (without src) from
index.html and checks that each PARSES without executing — no THREE/DOM/self needed. Handling depends on
the script's type:
  * classic scripts        -> wrapped in an uncalled function (parse-only, global-free)
  * type="module"          -> parsed as a real ES module, so top-level await/import is legal
  * type="importmap"/JSON  -> validated as JSON (an import map is JSON, not JavaScript)
Uses `node --check` if available (module-aware via the .mjs extension), else macOS JavaScriptCore's
`jsc` (classic scripts only; module scripts are parse-checked only under node). Exit 0 if everything
parses, nonzero on the first error. Used by CI and runnable locally (`python3 tools/check_syntax.py`)."""
import json, os, re, shutil, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
JSC = "/System/Library/Frameworks/JavaScriptCore.framework/Versions/Current/Helpers/jsc"

# type= values that mean "this block is JSON, not JavaScript" (import maps, speculation rules, data blocks)
JSON_TYPES = {"importmap", "application/importmap+json", "application/json", "speculationrules"}


def pick_checker():
    if shutil.which("node"):
        # node infers module (.mjs) vs classic script (.js) from the file extension; --check is parse-only.
        return "node", (lambda f: subprocess.run(["node", "--check", f], capture_output=True, text=True)), True
    if os.path.exists(JSC):
        # load() parses + defines the wrapped (uncalled) function -> parse-only, no runtime errors. jsc can't
        # parse-only an ES module, so module scripts are skipped under jsc (node covers them in CI).
        return "jsc", (lambda f: subprocess.run([JSC, "-e", f"load('{f}')"], capture_output=True, text=True)), False
    print("ERROR: no JS syntax checker found (need `node` or macOS `jsc`)")
    sys.exit(2)


def script_type(attrs):
    m = re.search(r'\btype\s*=\s*["\']?\s*([^"\'\s>]+)', attrs, re.I)
    return m.group(1).lower() if m else ""


def main():
    name, check, supports_modules = pick_checker()
    print(f"JS syntax gate via {name}")
    fails = 0
    with tempfile.TemporaryDirectory() as td:
        html = open(os.path.join(ROOT, "index.html"), encoding="utf-8").read()
        scripts = re.findall(r'<script(?![^>]*\bsrc=)([^>]*)>(.*?)</script>', html, re.S)
        for i, (attrs, body) in enumerate(scripts):
            typ = script_type(attrs)
            label = f"index.html inline <script #{i}> type={typ or '(classic)'} ({len(body)} chars)"

            if typ in JSON_TYPES:   # an import map / JSON data block is JSON, not JS — validate it as JSON
                try:
                    json.loads(body)
                    print(f"  OK   {label} [JSON]")
                except json.JSONDecodeError as e:
                    fails += 1
                    print(f"  FAIL {label} [JSON]")
                    sys.stderr.write(f"{e}\n")
                continue

            is_module = typ == "module"
            if is_module and not supports_modules:
                print(f"  SKIP {label} [module — parse-checked only under node]")
                continue

            if is_module:
                p = os.path.join(td, f"inline_{i}.mjs")     # .mjs -> node parses as an ES module
                open(p, "w", encoding="utf-8").write(body)  # no IIFE wrapper: a module has its own scope,
                #                                             and top-level await/import are legal here
            else:
                p = os.path.join(td, f"inline_{i}.js")
                open(p, "w", encoding="utf-8").write("(function(){\n" + body + "\n})")
            r = check(p)
            ok = r.returncode == 0
            print(f"  {'OK  ' if ok else 'FAIL'} {label}")
            if not ok:
                fails += 1
                sys.stderr.write((r.stderr or r.stdout or "").strip() + "\n")

        sw = os.path.join(ROOT, "sw.js")
        if os.path.exists(sw):
            p = os.path.join(td, "sw_wrapped.js")
            open(p, "w", encoding="utf-8").write("(function(){\n" + open(sw, encoding="utf-8").read() + "\n})")
            r = check(p)
            ok = r.returncode == 0
            print(f"  {'OK  ' if ok else 'FAIL'} sw.js")
            if not ok:
                fails += 1
                sys.stderr.write((r.stderr or r.stdout or "").strip() + "\n")

    if fails:
        print(f"\n{fails} syntax error(s)")
        sys.exit(1)
    print("all scripts parse cleanly")


if __name__ == "__main__":
    main()
