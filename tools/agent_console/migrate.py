#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
migrate.py - Agent 控制软件 SQLite 数据库迁移脚本（可重复执行 / 幂等）

用途：
  1. 校验数据库是否包含全部"审计日志"与"配置项"所需字段；
  2. 对已存在的旧库自动补齐缺失的表与列；
  3. 回填默认配置项，保证 `app.py` 启动后功能完整。

设计要点：
  - 幂等：可对同一数据库重复运行，已存在/已满足的字段不会重复添加或破坏数据；
  - 无损：只执行 CREATE TABLE IF NOT EXISTS 与 ALTER TABLE ... ADD COLUMN，
    绝不对已有列做修改或删除；
  - 兼容：同时兼容"全新库"（执行 schema.sql 建表）与"旧库"（增量补齐）。

用法：
  python migrate.py [db_path]
  默认 db_path 为环境变量 AGENT_CONSOLE_DB，否则为 <本脚本目录>/agent_console.db。
  退出码：0 = 迁移成功；1 = 参数错误；2 = 迁移过程出现异常。
"""
import os
import sqlite3
import sys

# 当前目录下自带 schema.sql（与 app.py 共用同一份定义）
BASE = os.path.dirname(os.path.abspath(__file__))
SCHEMA_PATH = os.path.join(BASE, "schema.sql")
DB_PATH = os.environ.get("AGENT_CONSOLE_DB", os.path.join(BASE, "agent_console.db"))

# —— 期望 schema（与 schema.sql 一致，用于校验/补齐） ——
REQUIRED_TABLES = {
    "agents": [
        "id", "name", "type", "description", "enabled",
        "safety_level", "accuracy", "model_path", "created_at", "updated_at",
    ],
    "training_runs": [
        "id", "agent_id", "agent_name", "source", "n_train", "n_test",
        "train_acc", "test_acc", "imbalance_ratio", "status",
        "started_at", "finished_at",
    ],
    "samples": ["id", "agent_name", "f1", "f2", "y", "split"],
    "config": ["key", "value"],          # 配置项（键值）
    "audit_log": ["id", "ts", "actor", "action", "detail"],  # 审计日志
}

# 审计日志/配置项字段的"存在性断言"（供用户核验清单用）
AUDIT_FIELDS = ["id", "ts", "actor", "action", "detail"]
CONFIG_FIELDS = ["key", "value"]

# 默认配置项（与 schema.sql 一致）
DEFAULT_CONFIG = {
    "n_train": "400",
    "n_test": "200",
    "fw_rate": "100",
    "fw_burst": "10",
    "fw_max_payload": "1024",
}


def init_db(conn):
    """全新库：执行 schema.sql 建表（若表已存在则跳过）。"""
    if not os.path.exists(SCHEMA_PATH):
        raise FileNotFoundError(f"找不到 schema.sql: {SCHEMA_PATH}")
    with open(SCHEMA_PATH, "r", encoding="utf-8") as f:
        conn.executescript(f.read())
    conn.commit()


def _table_columns(conn, table):
    """读取某表当前的实际列名集合。"""
    try:
        return {r[1] for r in conn.execute(f"PRAGMA table_info({table})").fetchall()}
    except sqlite3.Error:
        return set()


def _tables(conn):
    """读取当前数据库已有的表名集合。"""
    return {r[0] for r in conn.execute(
        "SELECT name FROM sqlite_master WHERE type='table'").fetchall()}


def migrate(db_path=DB_PATH, verbose=True):
    """执行迁移，返回变更统计 dict。"""
    report = {"created_tables": [], "added_columns": {},
              "backfilled_config": [], "validated": True, "errors": []}

    exists = os.path.exists(db_path)
    conn = sqlite3.connect(db_path)
    try:
        tables = _tables(conn)

        # 1) 全新库：直接按 schema.sql 建表
        if not tables:
            init_db(conn)
            tables = _tables(conn)   # 刷新，使后续校验/回填基于建表后的实际状态
            report["created_tables"] = sorted(REQUIRED_TABLES.keys())
            if verbose:
                print(f"[新建] 数据库 {db_path} 已按 schema.sql 初始化")

        # 2) 旧库：逐表补齐缺失列（只加列，不改列）
        for table, cols in REQUIRED_TABLES.items():
            if table not in tables:
                # 缺表则由 schema.sql 兜底（init_db 处理过的场景；此处以防万一）
                continue
            cur = _table_columns(conn, table)
            missing = [c for c in cols if c not in cur]
            if missing:
                report["added_columns"][table] = missing
                for c in missing:
                    conn.execute(f"ALTER TABLE {table} ADD COLUMN {c} TEXT")
                if verbose:
                    print(f"[补齐] 表 {table} 新增列: {', '.join(missing)}")

        # 3) 校验 audit_log 表字段
        if "audit_log" in tables:
            cur = _table_columns(conn, "audit_log")
            missing = [c for c in AUDIT_FIELDS if c not in cur]
            if missing:
                report["validated"] = False
                report["errors"].append(f"audit_log 缺少字段: {missing}")
        else:
            report["validated"] = False
            report["errors"].append("缺少 audit_log 表")

        # 4) 校验 config 表字段
        if "config" in tables:
            cur = _table_columns(conn, "config")
            missing = [c for c in CONFIG_FIELDS if c not in cur]
            if missing:
                report["validated"] = False
                report["errors"].append(f"config 缺少字段: {missing}")
        else:
            report["validated"] = False
            report["errors"].append("缺少 config 表")

        # 5) 回填默认配置（不覆盖已存在的值）
        if "config" in tables:
            for k, v in DEFAULT_CONFIG.items():
                row = conn.execute(
                    "SELECT 1 FROM config WHERE key=?", (k,)).fetchone()
                if not row:
                    conn.execute(
                        "INSERT OR IGNORE INTO config(key,value) VALUES(?,?)", (k, v))
                    report["backfilled_config"].append(k)
            conn.commit()
            if report["backfilled_config"] and verbose:
                print("[回填] 默认配置项: " + ", ".join(report["backfilled_config"]))

        # 汇总
        status = "OK" if report["validated"] and not report["errors"] else "INCOMPLETE"
        print(f"\n审计日志字段 ({len(AUDIT_FIELDS)}): {', '.join(AUDIT_FIELDS)}")
        print(f"配置项字段   ({len(CONFIG_FIELDS)}): {', '.join(CONFIG_FIELDS)}")
        print(f"迁移结果: {status}  (库文件: {db_path})")
        return report, status
    finally:
        conn.close()


def main(argv):
    if len(argv) > 2:
        print(__doc__)
        return 1
    db_path = argv[1] if len(argv) == 2 else DB_PATH
    try:
        _, status = migrate(db_path)
        return 0 if status == "OK" else 2
    except Exception as e:  # noqa: 捕获迁移异常
        print(f"[异常] 迁移失败: {e}")
        return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
