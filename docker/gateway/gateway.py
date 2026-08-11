#!/usr/bin/env python3
"""
ZhiOS-AF 云端 AI 服务网关

对应《15-Docker容器化构建与部署文档》《19-云端协议适配器规范》。
在 MCU 端 (固件) 的云端适配器 (openai/claude/a2a/mcp) 通过 HTTP 把请求
发给本网关；本网关再按协议代理到真实云服务 (OpenAI/Anthropic/A2A/MCP)。

设计要点（安全）：
  - 密钥不在固件中明文存储；MCU 只传密钥引用 (api_key_ref)。
  - 网关通过环境变量 OPENAI_API_KEY / CLAUDE_API_KEY 持有实际密钥，
    不写入仓库/镜像（运行时注入）。
  - 支持 mock 模式（GATEWAY_MOCK=1），无需真实密钥即可联调。

仅使用 Python 标准库，便于在精简容器内运行。
"""

import json
import os
import urllib.request
import urllib.error
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORT = int(os.environ.get("GATEWAY_PORT", "8080"))
MOCK = os.environ.get("GATEWAY_MOCK", "0") == "1"

OPENAI_ENDPOINT = os.environ.get(
    "OPENAI_ENDPOINT", "https://api.openai.com/v1/chat/completions")
OPENAI_KEY = os.environ.get("OPENAI_API_KEY", "")
OPENAI_MODEL = os.environ.get("OPENAI_MODEL", "gpt-4o-mini")

CLAUDE_ENDPOINT = os.environ.get(
    "CLAUDE_ENDPOINT", "https://api.anthropic.com/v1/messages")
CLAUDE_KEY = os.environ.get("CLAUDE_API_KEY", "")
CLAUDE_MODEL = os.environ.get("CLAUDE_MODEL", "claude-3-haiku")


def _http_post(url, headers, body, timeout=10):
    req = urllib.request.Request(url, data=body.encode("utf-8"),
                                 headers=headers, method="POST")
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return resp.status, resp.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode("utf-8", "replace")
    except Exception as e:  # noqa: BLE001
        return 0, str(e)


def route(protocol, payload, timeout):
    """按协议路由到真实云服务。payload 为 MCU 端适配器构造的请求体。"""
    # Mock 模式：直接返回固定结果，便于无密钥联调/测试
    if MOCK:
        return 200, {"ok": True, "protocol": protocol,
                     "reply": "mock:ack", "echo": payload[:80]}

    if protocol in ("openai", "openai-compatible"):
        body = payload if isinstance(payload, str) else json.dumps(payload)
        status, text = _http_post(OPENAI_ENDPOINT, {
            "Content-Type": "application/json",
            "Authorization": "Bearer " + OPENAI_KEY,
        }, body, timeout)
        return status, text

    if protocol == "claude":
        body = payload if isinstance(payload, str) else json.dumps(payload)
        status, text = _http_post(CLAUDE_ENDPOINT, {
            "Content-Type": "application/json",
            "x-api-key": CLAUDE_KEY,
            "anthropic-version": "2023-06-01",
        }, body, timeout)
        return status, text

    # A2A / MCP 为 JSON-RPC 风格；未配置真实端点时降级为 mock 提示
    if protocol in ("a2a", "mcp"):
        return 200, json.dumps({"ok": True, "protocol": protocol,
                                "note": "endpoint not configured, mock-ack"})

    return 400, json.dumps({"ok": False, "error": "unsupported protocol"})


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):  # 精简日志
        pass

    def _read_json(self):
        length = int(self.headers.get("Content-Length", 0))
        raw = self.rfile.read(length) if length else b"{}"
        try:
            return json.loads(raw.decode("utf-8") or "{}")
        except (json.JSONDecodeError, UnicodeDecodeError):
            return {"_raw": raw.decode("utf-8", "replace")}

    def _send(self, code, obj):
        data = json.dumps(obj).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        if self.path == "/health":
            self._send(200, {"ok": True, "service": "zhi-os-af-gateway",
                             "mock": MOCK})
        else:
            self._send(404, {"ok": False, "error": "not found"})

    def do_POST(self):
        req = self._read_json()
        protocol = req.get("protocol", "openai")
        payload = req.get("payload", req)
        timeout = int(req.get("timeout_ms", 10000)) / 1000.0
        status, reply = route(protocol, payload, timeout)
        if isinstance(reply, dict):
            self._send(status, reply)
        else:
            try:
                self._send(status, json.loads(reply))
            except (json.JSONDecodeError, TypeError):
                self._send(status, {"ok": status == 200, "raw": reply})


def main():
    print(f"[gateway] listen :{PORT} mock={MOCK}")
    ThreadingHTTPServer(("0.0.0.0", PORT), Handler).serve_forever()


if __name__ == "__main__":
    main()
