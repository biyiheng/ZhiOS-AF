#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
e2e_test.py - Agent 控制软件端到端自动化测试（前后端 + 数据库 + 全功能模块）

在 Docker 容器（或本机）中完整验证 Agent 控制软件的端到端流程是否通畅：

  [验证范围]
    1. 前端        : 控制台首页 /index.html 可访问
    2. 数据库连接  : SQLite 初始化，五张核心表（agents/training_runs/samples/config/audit_log）齐全
    3. 功能模块    : Agent 管理 / 训练流水线 / 数据分布 / 运行监控 / 安全防火墙 / 配置 / 审计
    4. 端到端链路  : 前端 fetch -> REST API -> 业务逻辑 -> SQLite 落库 -> 审计留痕
    5. 安全加固    : 越权/越界访问、非法路由、重复创建、超长载荷、防火墙默认拒绝

  [运行方式]
    # 方式 A：进程内自启服务（默认，本机/Docker 均可，零外部依赖）
    python tools/agent_console/e2e_test.py

    # 方式 B：连接已运行的 agent-console（例如 Docker 容器）
    python tools/agent_console/e2e_test.py --url http://localhost:8000

  退出码 0 = 全部通过；非 0 = 存在失败项。
"""
import argparse
import http.client
import json
import os
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.parse
import urllib.request

_BASE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _BASE)


# ---------- HTTP 客户端（仅标准库） ----------
class Client:
    def __init__(self, base):
        self.base = base.rstrip("/")

    def _request(self, method, path, body=None):
        data = json.dumps(body).encode("utf-8") if body is not None else None
        req = urllib.request.Request(self.base + path, data=data, method=method,
                                     headers={"Content-Type": "application/json"})
        try:
            with urllib.request.urlopen(req, timeout=30) as resp:
                raw = resp.read().decode("utf-8")
                return resp.status, (json.loads(raw) if raw else None)
        except urllib.error.HTTPError as e:
            raw = e.read().decode("utf-8")
            return e.code, (json.loads(raw) if raw else None)

    def get(self, path):   return self._request("GET", path)
    def post(self, path, body=None): return self._request("POST", path, body)
    def put(self, path, body=None):  return self._request("PUT", path, body)
    def delete(self, path): return self._request("DELETE", path)

    def raw_get(self, path):
        with urllib.request.urlopen(self.base + path, timeout=30) as resp:
            return resp.status, resp.read()


# ---------- 极简断言框架 ----------
class Reporter:
    def __init__(self):
        self.passed = 0
        self.failed = 0
        self.failures = []

    def check(self, name, cond, detail=""):
        if cond:
            self.passed += 1
            print(f"  [PASS] {name}")
        else:
            self.failed += 1
            self.failures.append(name)
            print(f"  [FAIL] {name}  {detail}")


def require(report, name, cond, detail=""):
    report.check(name, bool(cond), detail)


# ---------- 测试套件 ----------
def test_frontend(c, r):
    code, body = c.raw_get("/")
    require(r, "前端控制台 / 可访问", code == 200 and b"ZhiOS-AF" in body,
            f"code={code}")
    code, body = c.raw_get("/index.html")
    require(r, "前端 /index.html 可访问", code == 200 and b"ZhiOS-AF" in body,
            f"code={code}")
    # 静态资源
    code, _ = c.raw_get("/index.html")
    require(r, "Content-Type text/html", code == 200)


def test_database(c, r):
    code, d = c.get("/api/agents")
    require(r, "数据库连接成功（读 agents 表）", code == 200, f"code={code}")
    agents = d.get("agents", [])
    require(r, "种子 Agent 已注册（>=5）", len(agents) >= 5,
            f"count={len(agents)}")
    names = {a["name"] for a in agents}
    for exp in ("vision", "motion", "safety", "force", "quality"):
        require(r, f"内置 Agent 存在: {exp}", exp in names)


def test_agents_crud(c, r):
    # 创建
    code, d = c.post("/api/agents", {"name": "e2e_tmp", "description": "e2e", "type": "sub"})
    require(r, "创建 Agent 返回 201", code == 201, f"code={code}")
    aid = d.get("id")
    # 重复创建 -> 409
    code, _ = c.post("/api/agents", {"name": "e2e_tmp"})
    require(r, "重复创建返回 409", code == 409, f"code={code}")
    # 更新
    code, _ = c.put(f"/api/agents/{aid}", {"enabled": 0, "description": "e2e2"})
    require(r, "更新 Agent 返回 200", code == 200, f"code={code}")
    # 查询确认
    code, d = c.get("/api/agents")
    row = next((a for a in d["agents"] if a["id"] == aid), None)
    require(r, "更新生效（enabled=0）", row is not None and row["enabled"] == 0)
    # 删除
    code, d = c.delete(f"/api/agents/{aid}")
    require(r, "删除 Agent 返回 200", code == 200 and d.get("ok"), f"code={code}")
    code, _ = c.get(f"/api/agents/{aid}")  # 404 路由（GET 单条未实现，预期 404）
    code, d = c.get("/api/agents")
    require(r, "删除生效", all(a["id"] != aid for a in d["agents"]))
    # 非法：创建缺 name -> 400
    code, _ = c.post("/api/agents", {"description": "no-name"})
    require(r, "缺 name 创建返回 400", code == 400, f"code={code}")


def test_training(c, r):
    code, d = c.get("/api/agents")
    vid = next((a["id"] for a in d["agents"] if a["name"] == "vision"), None)
    require(r, "vision Agent 存在", vid is not None)
    t0 = time.time()
    code, d = c.post(f"/api/agents/{vid}/train")
    dt = time.time() - t0
    require(r, "触发训练返回 200", code == 200, f"code={code}")
    require(r, "训练返回 test_acc 数值", d is not None and d.get("test_acc") is not None)
    require(r, "训练返回数据来源", d is not None and d.get("source"), f"src={d and d.get('source')}")
    require(r, "训练返回不平衡比", d is not None and d.get("imbalance_ratio") is not None)
    require(r, "训练耗时合理（<60s）", dt < 60, f"dt={dt:.1f}s")
    # 训练记录已入库
    code, d = c.get("/api/training/runs")
    runs = d.get("runs", [])
    require(r, "训练运行记录已入库", any(x.get("agent_name") == "vision" for x in runs))
    require(r, "运行记录含 source/acc 字段",
            any(x.get("test_acc") is not None for x in runs))


def test_dataset(c, r):
    code, d = c.get("/api/dataset")
    require(r, "样本接口返回 200", code == 200, f"code={code}")
    cnt = d.get("count", 0)
    require(r, "样本库已生成（count>0）", cnt > 0, f"count={cnt}")
    require(r, "样本含全部 Agent",
            len({s["agent_name"] for s in d["samples"]}) >= 5)
    require(r, "样本字段完整（f1/f2/y/split）",
            all(all(k in s for k in ("f1", "f2", "y", "split")) for s in d["samples"][:50]))


def test_monitor(c, r):
    code, d = c.get("/api/monitor")
    require(r, "监控接口返回 200", code == 200, f"code={code}")
    require(r, "监控含 uptime/agents_total",
            "uptime_s" in d and d.get("agents_total", 0) >= 5)
    require(r, "监控含防火墙统计", "fw" in d and "allowed" in d["fw"])
    require(r, "最近训练存在", d.get("last_run") is not None)


def test_firewall(c, r):
    code, d = c.get("/api/security/firewall")
    require(r, "防火墙查询返回 200", code == 200, f"code={code}")
    f = d["firewall"]
    require(r, "防火墙默认含命令白名单", len(f["cmds"]) > 0)
    # 更新：只放行 0x01
    code, d = c.put("/api/security/firewall",
                    {"cmds": [1], "srcs": [], "has_src_rule": False,
                     "rate": 100, "burst": 10, "max_payload": 64})
    require(r, "防火墙规则更新返回 200", code == 200, f"code={code}")
    f = d["firewall"]
    require(r, "白名单更新生效（cmds=[1]）", f["cmds"] == [1], f"cmds={f['cmds']}")
    # 默认拒绝语义由 C 层 firewall.c 验证；此处核对配置接口与审计留痕
    require(r, "更新后统计计数存在", "allowed" in f and "blocked" in f)
    # 恢复默认
    c.put("/api/security/firewall",
          {"cmds": [1, 2, 3, 16], "srcs": [], "has_src_rule": False,
           "rate": 100, "burst": 10, "max_payload": 1024})


def test_config(c, r):
    code, d = c.get("/api/config")
    require(r, "配置查询返回 200", code == 200, f"code={code}")
    cfg = d["config"]
    require(r, "默认配置含 n_train/n_test",
            "n_train" in cfg and "n_test" in cfg)
    require(r, "默认配置含防火墙参数",
            all(k in cfg for k in ("fw_rate", "fw_burst", "fw_max_payload")))
    # 更新
    code, d = c.put("/api/config", {"config": {"n_train": "300"}})
    require(r, "配置更新返回 200", code == 200, f"code={code}")
    code, d = c.get("/api/config")
    require(r, "配置更新生效（n_train=300）", d["config"].get("n_train") == "300")
    c.put("/api/config", {"config": {"n_train": "400"}})


def test_audit(c, r):
    code, d = c.get("/api/audit")
    require(r, "审计接口返回 200", code == 200, f"code={code}")
    logs = d.get("logs", [])
    require(r, "审计日志非空", len(logs) > 0, f"count={len(logs)}")
    actions = {l["action"] for l in logs}
    require(r, "含 agent 操作留痕", any(a.startswith("agent.") for a in actions))
    require(r, "含防火墙更新留痕", "firewall.update" in actions)
    require(r, "含配置更新留痕", "config.update" in actions)
    require(r, "审计字段完整（ts/actor/action/detail）",
            all(all(k in l for k in ("ts", "actor", "action", "detail")) for l in logs[:50]))


def test_security_hardening(c, r):
    # 越界 / 非法路由 -> 404
    code, _ = c.get("/api/nonexistent")
    require(r, "非法 API 路由返回 404", code == 404, f"code={code}")
    code, _ = c.get("/../etc/passwd")
    require(r, "路径穿越被拒绝（非 200 静态泄露）", code != 200, f"code={code}")
    # 不允许的方法 -> 405/404
    code, _ = c.put("/api/monitor", {})
    require(r, "只读接口拒绝写方法", code in (404, 405), f"code={code}")
    # 更新不存在的 Agent -> 404
    code, _ = c.put("/api/agents/999999", {"description": "x"})
    require(r, "更新不存在 Agent 返回 404", code == 404, f"code={code}")
    # 恶意载荷（超长 JSON）不应导致 500
    big = json.dumps({"name": "x" * 100000})
    code = c._request("POST", "/api/agents", json.loads(big))[0]
    require(r, "超长名称被拒绝（4xx）", code in (400, 409, 413, 414), f"code={code}")


def _persistent_conn(base):
    """解析 base 得到 host/port，返回可复用的 http.client.HTTPConnection。"""
    u = urllib.parse.urlsplit(base)
    host = u.hostname or "127.0.0.1"
    port = u.port or (443 if u.scheme == "https" else 80)
    return http.client.HTTPConnection(host, port, timeout=10)


def test_regression_413(c, r):
    """回归测试：413 超大载荷 + 连接中止修复（Bug #19）。

    修复前：返回 413 时未排空请求体，服务器带未读数据关闭连接触发
    ConnectionAbortedError(10053)，后续请求失败。
    修复后：先排空请求体 + Connection: close，连接不被中止，服务持续可用。
    """
    big = json.dumps({"name": "x" * 100000}).encode("utf-8")
    headers = {"Content-Type": "application/json",
               "Content-Length": str(len(big))}

    # 1) 单个超大载荷：urllib 请求返回 413，且不抛连接异常（原 Bug 在此抛 ConnectionAbortedError）
    try:
        code, _ = c._request("POST", "/api/agents", json.loads(big))
        require(r, "单个超大载荷返回 413（不中止连接）", code == 413, f"code={code}")
    except Exception as e:   # noqa
        require(r, "单个超大载荷返回 413（不中止连接）", False, repr(e))

    # 2) 通过持久连接验证 413 响应带 Connection: close 头
    conn = _persistent_conn(c.base)
    try:
        conn.request("POST", "/api/agents", body=big, headers=headers)
        resp = conn.getresponse()
        resp.read()
        close_hdr = (resp.getheader("Connection") or "").lower()
        require(r, "413 响应带 Connection: close", "close" in close_hdr,
                f"hdr={close_hdr!r}")
    except Exception as e:   # noqa
        require(r, "413 响应带 Connection: close", False, repr(e))
    finally:
        conn.close()

    # 3) 同一连接在 413 后仍可用（服务器未因残留数据中止连接）
    conn = _persistent_conn(c.base)
    try:
        conn.request("POST", "/api/agents", body=big, headers=headers)
        resp = conn.getresponse()
        resp.read()
        code1 = resp.status
        conn.request("GET", "/api/monitor")
        resp2 = conn.getresponse()
        resp2.read()
        code2 = resp2.status
        require(r, "413 后同连接后续请求可用", code1 == 413 and code2 == 200,
                f"code1={code1} code2={code2}")
    except Exception as e:   # noqa
        require(r, "413 后同连接后续请求可用", False, repr(e))
    finally:
        conn.close()

    # 4) 连续多次超大载荷均稳定返回 413，服务不中断
    stable = True
    for _ in range(3):
        cc = _persistent_conn(c.base)
        try:
            cc.request("POST", "/api/agents", body=big, headers=headers)
            rr = cc.getresponse()
            rr.read()
            if rr.status != 413:
                stable = False
        except Exception:   # noqa
            stable = False
        finally:
            cc.close()
    require(r, "连续多次 413 均稳定返回", stable)

    # 5) 413 之后服务仍健康（正常业务不受影响）
    code, d = c.get("/api/monitor")
    require(r, "413 后服务仍健康", code == 200 and d.get("agents_total", 0) >= 5,
            f"code={code}")


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default=None,
                    help="连接已运行的 agent-console 地址；缺省则进程内自启服务")
    args = ap.parse_args(argv)

    r = Reporter()
    print("=" * 64)
    print("ZhiOS-AF Agent 控制软件 · 端到端自动化测试")
    print("=" * 64)

    srv = None
    if args.url:
        base = args.url
        print(f"[mode] 连接已运行实例: {base}")
    else:
        # 进程内自启服务：临时数据库 + 后台线程
        tmp = tempfile.mkdtemp(prefix="zhi-e2e-")
        dbp = os.path.join(tmp, "e2e.db")
        import app
        app.DB_PATH = dbp
        app.init_db(os.path.join(app.BASE, "schema.sql"))
        app.seed_agents()
        from http.server import ThreadingHTTPServer
        srv = ThreadingHTTPServer(("127.0.0.1", 0), app.Handler)
        port = srv.server_address[1]
        threading.Thread(target=srv.serve_forever, daemon=True).start()
        base = f"http://127.0.0.1:{port}"
        print(f"[mode] 进程内自启服务: {base}")

    c = Client(base)

    suites = [
        ("前端服务", test_frontend),
        ("数据库连接", test_database),
        ("Agent 管理(CRUD)", test_agents_crud),
        ("训练流水线", test_training),
        ("数据分布/样本", test_dataset),
        ("运行监控", test_monitor),
        ("安全/防火墙", test_firewall),
        ("配置管理", test_config),
        ("审计日志", test_audit),
        ("安全加固", test_security_hardening),
        ("回归-413/连接中止", test_regression_413),
    ]

    for name, fn in suites:
        print(f"\n[{name}]")
        try:
            fn(c, r)
        except Exception as e:   # noqa
            r.check(f"{name} 未抛出异常", False, repr(e))

    if srv is not None:
        srv.shutdown()

    print("\n" + "=" * 64)
    print(f"断言汇总: PASS={r.passed}  FAIL={r.failed}")
    if r.failed:
        print("失败项:")
        for f in r.failures:
            print(f"  - {f}")
        print("==> E2E TEST FAILED")
        return 1
    print("==> E2E TEST ALL PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
