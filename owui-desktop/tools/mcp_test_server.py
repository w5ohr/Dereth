#!/usr/bin/env python3
"""A tiny dependency-free MCP server used by `Open WebUI --mcptest` and handy
as a smoke test for the MCP tab: add it with command `python3` and this file
as the argument.

    python3 mcp_test_server.py              # stdio transport
    python3 mcp_test_server.py --http 8765  # Streamable HTTP on :8765/mcp
    python3 mcp_test_server.py --http 8765 --sse   # replies as SSE events

Tools: add, echo, slow, fail, env, roots (asks the client for roots/list).
One resource (test://greeting) and one prompt (greet).
"""
import json
import os
import sys
import threading
import time
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PROTOCOL = "2025-06-18"

TOOLS = [
    {"name": "add", "description": "Add two numbers and return the sum.",
     "inputSchema": {"type": "object", "properties": {"a": {"type": "number"}, "b": {"type": "number"}},
                     "required": ["a", "b"]}},
    {"name": "echo", "description": "Return the text you were given.",
     "inputSchema": {"type": "object", "properties": {"text": {"type": "string"}}, "required": ["text"]}},
    {"name": "slow", "description": "Sleep for `seconds` then return.",
     "inputSchema": {"type": "object", "properties": {"seconds": {"type": "number"}}, "required": ["seconds"]}},
    {"name": "fail", "description": "Always reports a tool error.",
     "inputSchema": {"type": "object", "properties": {}}},
    {"name": "env", "description": "Return the value of an environment variable.",
     "inputSchema": {"type": "object", "properties": {"name": {"type": "string"}}, "required": ["name"]}},
    {"name": "roots", "description": "Ask the client for its workspace roots.",
     "inputSchema": {"type": "object", "properties": {}}},
]


class Session:
    """Protocol state shared by both transports. `send` delivers a message to
    the client; `request_client` performs a server->client request."""

    def __init__(self, send):
        self.send = send
        self.initialized = False
        self.next_id = 1000
        self.waiting = {}
        self.lock = threading.Lock()

    def request_client(self, method, params, timeout=10):
        with self.lock:
            rid = self.next_id
            self.next_id += 1
            event = threading.Event()
            slot = {"event": event, "response": None}
            self.waiting[rid] = slot
        self.send({"jsonrpc": "2.0", "id": rid, "method": method, "params": params})
        if not event.wait(timeout):
            with self.lock:
                self.waiting.pop(rid, None)
            return None
        return slot["response"]

    def handle(self, msg):
        """Returns the reply message, or None for notifications/responses."""
        if "method" not in msg:
            # A response to one of our own requests.
            slot = None
            with self.lock:
                slot = self.waiting.pop(msg.get("id"), None)
            if slot:
                slot["response"] = msg
                slot["event"].set()
            return None
        method = msg["method"]
        params = msg.get("params") or {}
        rid = msg.get("id")
        if rid is None:
            if method == "notifications/initialized":
                self.initialized = True
                self.send({"jsonrpc": "2.0", "method": "notifications/message",
                           "params": {"level": "info", "data": "test server ready"}})
            return None
        try:
            result = self.dispatch(method, params)
        except ToolError as e:
            return {"jsonrpc": "2.0", "id": rid, "error": {"code": e.code, "message": str(e)}}
        return {"jsonrpc": "2.0", "id": rid, "result": result}

    def dispatch(self, method, params):
        if method == "initialize":
            return {"protocolVersion": params.get("protocolVersion", PROTOCOL),
                    "capabilities": {"tools": {"listChanged": True}, "resources": {}, "prompts": {},
                                     "logging": {}},
                    "serverInfo": {"name": "mcp-test-server", "version": "1.0"},
                    "instructions": "A test server. The add tool is exact; prefer it for arithmetic."}
        if method == "ping":
            return {}
        if method == "tools/list":
            return {"tools": TOOLS}
        if method == "tools/call":
            return self.call_tool(params.get("name"), params.get("arguments") or {})
        if method == "resources/list":
            return {"resources": [{"uri": "test://greeting", "name": "Greeting",
                                   "description": "A fixed greeting", "mimeType": "text/plain"}]}
        if method == "resources/read":
            if params.get("uri") != "test://greeting":
                raise ToolError("unknown resource", -32002)
            return {"contents": [{"uri": "test://greeting", "mimeType": "text/plain",
                                  "text": "Hello from the MCP test server."}]}
        if method == "prompts/list":
            return {"prompts": [{"name": "greet", "description": "Greet someone",
                                 "arguments": [{"name": "name", "required": True}]}]}
        if method == "prompts/get":
            name = (params.get("arguments") or {}).get("name", "friend")
            return {"description": "Greet someone",
                    "messages": [{"role": "user", "content": {"type": "text", "text": f"Say hello to {name}."}}]}
        raise ToolError(f"Method not found: {method}", -32601)

    def call_tool(self, name, args):
        if name == "add":
            total = float(args["a"]) + float(args["b"])
            text = str(int(total)) if total.is_integer() else str(total)
            return {"content": [{"type": "text", "text": text}],
                    "structuredContent": {"sum": total}}
        if name == "echo":
            return {"content": [{"type": "text", "text": str(args.get("text", ""))}]}
        if name == "slow":
            time.sleep(float(args.get("seconds", 1)))
            return {"content": [{"type": "text", "text": "done"}]}
        if name == "fail":
            return {"content": [{"type": "text", "text": "this tool always fails"}], "isError": True}
        if name == "env":
            return {"content": [{"type": "text", "text": os.environ.get(str(args.get("name", "")), "")}]}
        if name == "roots":
            response = self.request_client("roots/list", {})
            roots = (response or {}).get("result", {}).get("roots", [])
            return {"content": [{"type": "text", "text": json.dumps(roots)}]}
        raise ToolError(f"Unknown tool: {name}", -32602)


class ToolError(Exception):
    def __init__(self, message, code=-32000):
        super().__init__(message)
        self.code = code


# ------------------------------------------------------------------ stdio

def serve_stdio():
    out_lock = threading.Lock()

    def send(message):
        with out_lock:
            sys.stdout.write(json.dumps(message) + "\n")
            sys.stdout.flush()

    session = Session(send)
    sys.stderr.write("mcp_test_server: stdio up\n")
    sys.stderr.flush()

    def work(msg):
        reply = session.handle(msg)
        if reply is not None:
            send(reply)

    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            msg = json.loads(line)
        except json.JSONDecodeError:
            continue
        # Each request on its own thread so `slow` doesn't block `ping`, and so
        # the roots tool can wait for the client's answer.
        threading.Thread(target=work, args=(msg,), daemon=True).start()


# ------------------------------------------------------------------ http

def serve_http(port, as_sse):
    sessions = {}
    lock = threading.Lock()

    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def log_message(self, *args):
            pass

        def _session(self, create):
            sid = self.headers.get("Mcp-Session-Id")
            with lock:
                if sid and sid in sessions:
                    return sid, sessions[sid]
                if create:
                    sid = uuid.uuid4().hex
                    pending = []
                    sessions[sid] = Session(pending.append)
                    sessions[sid].pending = pending
                    return sid, sessions[sid]
            return None, None

        def _respond(self, status, body=b"", content_type="application/json", extra=None):
            self.send_response(status)
            if body:
                self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(body)))
            for k, v in (extra or {}).items():
                self.send_header(k, v)
            self.end_headers()
            if body:
                self.wfile.write(body)

        def do_POST(self):
            length = int(self.headers.get("Content-Length", "0"))
            raw = self.rfile.read(length)
            try:
                msg = json.loads(raw)
            except json.JSONDecodeError:
                return self._respond(400, b'{"error":"bad json"}')
            is_init = msg.get("method") == "initialize"
            sid, session = self._session(create=is_init)
            if session is None:
                return self._respond(404, b'{"error":"no session"}')
            reply = session.handle(msg)
            extra = {"Mcp-Session-Id": sid} if is_init else {}
            if reply is None:
                return self._respond(202, b"", extra=extra)
            # Any server->client messages queued by the handler ride along.
            queued = list(session.pending)
            session.pending.clear()
            if as_sse:
                body = ""
                for m in queued + [reply]:
                    body += "event: message\ndata: " + json.dumps(m) + "\n\n"
                return self._respond(200, body.encode(), "text/event-stream", extra)
            return self._respond(200, json.dumps(reply).encode(), extra=extra)

        def do_DELETE(self):
            sid = self.headers.get("Mcp-Session-Id")
            with lock:
                sessions.pop(sid, None)
            self._respond(200, b"{}")

        def do_GET(self):
            self._respond(405, b'{"error":"no GET stream"}')

    server = ThreadingHTTPServer(("127.0.0.1", port), Handler)
    sys.stderr.write(f"mcp_test_server: http on {server.server_address[1]}\n")
    sys.stderr.flush()
    server.serve_forever()


if __name__ == "__main__":
    if "--http" in sys.argv:
        port = int(sys.argv[sys.argv.index("--http") + 1])
        serve_http(port, "--sse" in sys.argv)
    else:
        serve_stdio()
