#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
app.py - Agent 控制软件后端（纯 Python 标准库，零第三方依赖）

功能模块（详见《agent控制软件技术文档.md》）：
  1. Agent 管理        : 增删改查（agents 表）
  2. 训练流水线        : 触发训练 + 查看运行记录（trainer + training_runs 表）
  3. 数据管理          : 样本查看 / 类别分布
  4. 运行监控          : 存活状态 / Agent 计数 / 最近训练
  5. 安全 / 防火墙     : 查看与更新防火墙规则（默认拒绝 + 白名单 + 限速 + 长度上限）
  6. 配置管理          : 键值配置（config 表）
  7. 审计日志          : 操作留痕（audit_log 表）

运行：
    python app.py [--port 8000] [--db agent_console.db] [--seed-agents]
访问 http://localhost:8000 打开前端控制台。
"""
import argparse
import json
import os
import sqlite3
import sys
import time
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import trainer

BASE = os.path.dirname(os.path.abspath(__file__))
STATIC = os.path.join(BASE, "static")
DB_PATH = os.environ.get("AGENT_CONSOLE_DB", os.path.join(BASE, "agent_console.db"))

FW_DEFAULT_CMDS = [0x01, 0x02, 0x03, 0x10]   # 受信任命令白名单（示例）
MAX_BODY_BYTES = 64 * 1024                  # 请求体大小上限（防恶意超大载荷）
MAX_NAME_LEN = 64                           # Agent 名称长度上限（防超长输入）
FW_VERDICTS = {
    0: "ALLOW",
    1: "BLOCK_CMD",
    2: "BLOCK_SRC",
    3: "BLOCK_RATE",
    4: "BLOCK_LEN",
}

# ---- 防火墙运行态（对应 firewall.c 语义） ----
class FwState:
    def __init__(self):
        self.cmds = set(FW_DEFAULT_CMDS)
        self.srcs = set()
        self.has_src_rule = False
        self.max_payload = 1024
        self.rate = 100          # tokens/sec
        self.burst = 10
        self.tokens = self.burst * 1000
        self.capacity = self.burst * 1000
        self.tick = 0
        self.allowed = self.blocked = self.dropped = 0
        self.last_refill = 0

    def refill(self):
        el = max(0, self.tick - self.last_refill)
        self.last_refill = self.tick
        self.tokens = min(self.capacity, self.tokens + self.rate * el)

    def config(self, rate=None, burst=None, max_payload=None):
        if rate is not None:
            self.rate = max(0, int(rate))
            self.burst = max(1, int(burst)) if burst else self.burst
            self.capacity = self.burst * 1000
            self.tokens = self.capacity
        if max_payload is not None:
            self.max_payload = max(0, int(max_payload))

    def check(self, cmd, src=0, plen=0):
        self.allowed += 1
        if cmd not in self.cmds:
            self.blocked += 1; self.allowed -= 1; return "BLOCK_CMD"
        if self.has_src_rule and src not in self.srcs:
            self.blocked += 1; self.allowed -= 1; return "BLOCK_SRC"
        if self.max_payload and plen > self.max_payload:
            self.blocked += 1; self.allowed -= 1; return "BLOCK_LEN"
        self.refill()
        if self.tokens < 1000:
            self.dropped += 1; self.allowed -= 1; return "BLOCK_RATE"
        self.tokens -= 1000
        return "ALLOW"

    def to_dict(self):
        return {
            "cmds": sorted(self.cmds),
            "srcs": sorted(self.srcs),
            "has_src_rule": self.has_src_rule,
            "max_payload": self.max_payload,
            "rate": self.rate,
            "burst": self.burst,
            "allowed": self.allowed,
            "blocked": self.blocked,
            "dropped": self.dropped,
        }


FW = FwState()
UPTIME_START = time.time()


# ---- 数据库助手 ----
def db():
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    conn.execute("PRAGMA foreign_keys = ON")
    return conn


def init_db(schema_path):
    with open(schema_path, "r", encoding="utf-8") as f:
        sql = f.read()
    conn = db()
    conn.executescript(sql)
    conn.commit()
    conn.close()


def seed_agents():
    names = trainer.AGENTS
    desc = {
        "vision": "视觉感知与识别",
        "motion": "运动/轨迹规划",
        "safety": "安全校验",
        "force": "柔顺力控",
        "quality": "质检评分",
    }
    conn = db()
    for n in names:
        conn.execute(
            "INSERT OR IGNORE INTO agents(name,type,description) VALUES(?,?,?)",
            (n, "sub", desc.get(n, n)))
    conn.commit()
    conn.close()


def audit(conn, action, detail):
    conn.execute("INSERT INTO audit_log(action,detail) VALUES(?,?)", (action, detail))


# ---- 请求处理器 ----
class Handler(BaseHTTPRequestHandler):
    def _send(self, code, obj, ctype="application/json; charset=utf-8", close=False):
        body = obj if isinstance(obj, bytes) else json.dumps(obj, ensure_ascii=False).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        if close:
            self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(body)

    def _drain_body(self, n, cap=1 * 1024 * 1024):
        """分块丢弃请求体，避免连接残留未读数据被中止；限制读取量防滥用。"""
        remain = min(n, cap)
        chunk = 8192
        while remain > 0:
            k = min(remain, chunk)
            try:
                if not self.rfile.read(k):
                    break
            except Exception:   # noqa: 连接异常则停止排空
                break
            remain -= k

    def _read_json(self):
        n = int(self.headers.get("Content-Length", 0))
        if n <= 0:
            return {}
        if n > MAX_BODY_BYTES:
            self._drain_body(n)   # 先排空请求体，保证连接不被中止
            self._send(413, {"error": "payload too large"}, close=True)
            return None
        try:
            return json.loads(self.rfile.read(n).decode("utf-8"))
        except (ValueError, UnicodeDecodeError):
            self._send(400, {"error": "invalid json"}, close=True)
            return None

    def _route(self, method, path, query, body):
        parts = [p for p in path.split("/") if p]
        # /api/agents, /api/agents/{id}, /api/agents/{id}/train
        if method == "GET" and parts == ["api", "agents"]:
            return self._list_agents()
        if method == "POST" and parts == ["api", "agents"]:
            return self._create_agent(body)
        if method == "PUT" and parts[:2] == ["api", "agents"] and len(parts) == 3:
            return self._update_agent(parts[2], body)
        if method == "DELETE" and parts[:2] == ["api", "agents"] and len(parts) == 3:
            return self._delete_agent(parts[2])
        if method == "POST" and parts[:2] == ["api", "agents"] and len(parts) == 4 and parts[3] == "train":
            return self._train_agent(parts[2])
        if method == "GET" and parts == ["api", "training", "runs"]:
            return self._list_runs()
        if method == "GET" and parts == ["api", "dataset"]:
            return self._dataset()
        if method == "GET" and parts == ["api", "monitor"]:
            return self._monitor()
        if method == "GET" and parts == ["api", "security", "firewall"]:
            return self._get_firewall()
        if method == "PUT" and parts == ["api", "security", "firewall"]:
            return self._update_firewall(body)
        if method == "GET" and parts == ["api", "config"]:
            return self._get_config()
        if method == "PUT" and parts == ["api", "config"]:
            return self._update_config(body)
        if method == "GET" and parts == ["api", "audit"]:
            return self._audit()
        return 404, {"error": "not found"}

    # ---- 业务实现 ----
    def _list_agents(self):
        conn = db()
        rows = conn.execute("SELECT * FROM agents ORDER BY id").fetchall()
        conn.close()
        return 200, {"agents": [dict(r) for r in rows]}

    def _create_agent(self, body):
        name = (body.get("name") or "").strip()
        if not name:
            return 400, {"error": "name required"}
        if len(name) > MAX_NAME_LEN:
            return 400, {"error": "name too long"}
        conn = db()
        try:
            cur = conn.execute(
                "INSERT INTO agents(name,type,description,safety_level) VALUES(?,?,?,?)",
                (name, body.get("type", "sub"), body.get("description", ""),
                 body.get("safety_level", 1)))
            audit(conn, "agent.create", name)
            conn.commit()
            return 201, {"id": cur.lastrowid}
        except sqlite3.IntegrityError:
            return 409, {"error": "agent exists"}
        finally:
            conn.close()

    def _update_agent(self, aid, body):
        conn = db()
        if not conn.execute("SELECT id FROM agents WHERE id=?", (aid,)).fetchone():
            conn.close()
            return 404, {"error": "not found"}
        fields = ["name", "type", "description", "enabled", "safety_level", "model_path"]
        sets, vals = [], []
        for f in fields:
            if f in body:
                sets.append(f"{f}=?")
                vals.append(body[f])
        sets.append("updated_at=datetime('now')")
        if sets:
            conn.execute(f"UPDATE agents SET {','.join(sets)} WHERE id=?", (*vals, aid))
        audit(conn, "agent.update", str(aid))
        conn.commit()
        conn.close()
        return 200, {"ok": True}

    def _delete_agent(self, aid):
        conn = db()
        cur = conn.execute("DELETE FROM agents WHERE id=?", (aid,))
        audit(conn, "agent.delete", str(aid))
        conn.commit()
        conn.close()
        return 200, {"ok": cur.rowcount > 0}

    def _train_agent(self, aid):
        conn = db()
        row = conn.execute("SELECT * FROM agents WHERE id=?", (aid,)).fetchone()
        if not row:
            conn.close()
            return 404, {"error": "not found"}
        cfg = dict(conn.execute("SELECT key,value FROM config").fetchall())
        n_train = int(cfg.get("n_train", 400))
        n_test = int(cfg.get("n_test", 200))
        conn.execute("INSERT INTO training_runs(agent_id,agent_name,status) VALUES(?,?,'running')",
                     (aid, row["name"]))
        conn.commit()
        # 训练全部 Agent（控制台按 Agent 维度触发，此处返回该 Agent 结果）
        res = trainer.train_and_evaluate(n_train, n_test)
        acc = res["agents"][row["name"]]["test_acc"]
        train_acc = res["agents"][row["name"]]["train_acc"]
        ir = res["imbalance"]["summary"][row["name"]]["imbalance_ratio"]
        conn.execute(
            "UPDATE training_runs SET source=?, n_train=?, n_test=?, train_acc=?, test_acc=?, "
            "imbalance_ratio=?, status='done', finished_at=datetime('now') "
            "WHERE agent_id=? AND status='running'",
            (res["source"], n_train, n_test, train_acc, acc, ir, aid))
        conn.execute("UPDATE agents SET accuracy=? WHERE id=?", (acc, aid))
        audit(conn, "agent.train", f"{row['name']} acc={acc}")
        conn.commit()
        conn.close()
        return 200, {"agent": row["name"], "test_acc": acc, "train_acc": train_acc,
                     "source": res["source"], "imbalance_ratio": ir}

    def _list_runs(self):
        conn = db()
        rows = conn.execute("SELECT * FROM training_runs ORDER BY id DESC LIMIT 100").fetchall()
        conn.close()
        return 200, {"runs": [dict(r) for r in rows]}

    def _dataset(self):
        conn = db()
        rows = conn.execute("SELECT agent_name,f1,f2,y,split FROM samples").fetchall()
        conn.close()
        # 若样本库为空则即时生成并入库
        if not rows:
            self._populate_samples()
            conn = db()
            rows = conn.execute("SELECT agent_name,f1,f2,y,split FROM samples").fetchall()
            conn.close()
        data = [dict(r) for r in rows]
        return 200, {"samples": data, "count": len(data)}

    def _populate_samples(self):
        res = trainer.acquire_data()
        train, test = res[0], res[1]
        conn = db()
        for name, samples in train.items():
            for s in samples:
                conn.execute("INSERT INTO samples(agent_name,f1,f2,y,split) VALUES(?,?,?,?,'train')",
                             (name, s["f1"], s["f2"], s["y"]))
        for name, samples in test.items():
            for s in samples:
                conn.execute("INSERT INTO samples(agent_name,f1,f2,y,split) VALUES(?,?,?,?,'test')",
                             (name, s["f1"], s["f2"], s["y"]))
        conn.commit()
        conn.close()

    def _monitor(self):
        conn = db()
        total = conn.execute("SELECT COUNT(*) c FROM agents").fetchone()["c"]
        enabled = conn.execute("SELECT COUNT(*) c FROM agents WHERE enabled=1").fetchone()["c"]
        last = conn.execute("SELECT * FROM training_runs ORDER BY id DESC LIMIT 1").fetchone()
        conn.close()
        return 200, {
            "uptime_s": int(time.time() - UPTIME_START),
            "agents_total": total,
            "agents_enabled": enabled,
            "last_run": dict(last) if last else None,
            "fw": FW.to_dict(),
        }

    def _get_firewall(self):
        return 200, {"firewall": FW.to_dict(),
                     "verdicts": FW_VERDICTS}

    def _update_firewall(self, body):
        cmd_mode = body.get("cmd_mode", "whitelist")   # whitelist / allow_all
        if cmd_mode == "allow_all":
            FW.cmds = set(range(0x10000))
        elif "cmds" in body:
            FW.cmds = set(int(c) for c in body["cmds"])
        if "srcs" in body:
            FW.srcs = set(int(s) for s in body["srcs"])
        if "has_src_rule" in body:
            FW.has_src_rule = bool(body["has_src_rule"])
        FW.config(rate=body.get("rate"), burst=body.get("burst"),
                  max_payload=body.get("max_payload"))
        conn = db()
        audit(conn, "firewall.update", json.dumps(FW.to_dict()))
        conn.commit()
        conn.close()
        return 200, {"firewall": FW.to_dict()}

    def _get_config(self):
        conn = db()
        rows = conn.execute("SELECT key,value FROM config").fetchall()
        conn.close()
        return 200, {"config": {r["key"]: r["value"] for r in rows}}

    def _update_config(self, body):
        conn = db()
        for k, v in body.get("config", {}).items():
            conn.execute("INSERT OR REPLACE INTO config(key,value) VALUES(?,?)", (k, str(v)))
        audit(conn, "config.update", json.dumps(body.get("config", {})))
        conn.commit()
        conn.close()
        return 200, {"ok": True}

    def _audit(self):
        conn = db()
        rows = conn.execute("SELECT * FROM audit_log ORDER BY id DESC LIMIT 100").fetchall()
        conn.close()
        return 200, {"logs": [dict(r) for r in rows]}

    # ---- HTTP 分发 ----
    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path
        if path == "/" or path == "/index.html":
            try:
                with open(os.path.join(STATIC, "index.html"), "rb") as f:
                    return self._send(200, f.read(), "text/html; charset=utf-8")
            except FileNotFoundError:
                return self._send(404, b"index.html missing", "text/plain")
        if path.startswith("/api/"):
            code, obj = self._route("GET", path, parsed.query, {})
            return self._send(code, obj)
        # 静态资源
        rel = path.lstrip("/")
        fp = os.path.join(STATIC, rel)
        if os.path.isfile(fp):
            with open(fp, "rb") as f:
                ctype = "application/octet-stream"
                if fp.endswith(".js"):
                    ctype = "application/javascript"
                elif fp.endswith(".css"):
                    ctype = "text/css"
                return self._send(200, f.read(), ctype)
        return self._send(404, {"error": "not found"})

    def do_POST(self):
        parsed = urllib.parse.urlparse(self.path)
        body = self._read_json()
        if body is None:   # _read_json 已发送 400/413
            return
        code, obj = self._route("POST", parsed.path, parsed.query, body)
        self._send(code, obj)

    def do_PUT(self):
        parsed = urllib.parse.urlparse(self.path)
        body = self._read_json()
        if body is None:   # _read_json 已发送 400/413
            return
        code, obj = self._route("PUT", parsed.path, parsed.query, body)
        self._send(code, obj)

    def do_DELETE(self):
        parsed = urllib.parse.urlparse(self.path)
        code, obj = self._route("DELETE", parsed.path, parsed.query, {})
        self._send(code, obj)

    def log_message(self, *args):   # 精简日志
        pass


def main():
    global DB_PATH
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8000)
    ap.add_argument("--db", default=DB_PATH)
    ap.add_argument("--seed-agents", action="store_true",
                    help="首次运行时注册 5 个内置子 Agent")
    args = ap.parse_args()
    DB_PATH = args.db
    init_db(os.path.join(BASE, "schema.sql"))
    if args.seed_agents:
        seed_agents()
    server = ThreadingHTTPServer(("0.0.0.0", args.port), Handler)
    print(f"[agent_console] serving on http://localhost:{args.port}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n[agent_console] stopped")


if __name__ == "__main__":
    main()
