-- =============================================================
-- Agent 控制软件数据库 Schema（SQLite）
-- 对应《agent控制软件技术文档.md》
-- 表：agents / training_runs / samples / config / audit_log
-- =============================================================

-- Agent 注册表
CREATE TABLE IF NOT EXISTS agents (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    name         TEXT UNIQUE NOT NULL,          -- 唯一名称，如 vision
    type         TEXT NOT NULL DEFAULT 'sub',   -- sub / auto
    description  TEXT DEFAULT '',
    enabled      INTEGER NOT NULL DEFAULT 1,    -- 1 启用 / 0 停用
    safety_level INTEGER NOT NULL DEFAULT 1,    -- 安全等级 1-3
    accuracy     REAL,                          -- 最近一次测试准确率
    model_path   TEXT DEFAULT '',
    created_at   TEXT DEFAULT (datetime('now')),
    updated_at   TEXT DEFAULT (datetime('now'))
);

-- 训练运行记录
CREATE TABLE IF NOT EXISTS training_runs (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    agent_id        INTEGER,
    agent_name      TEXT NOT NULL,
    source          TEXT,                       -- 数据来源
    n_train         INTEGER,
    n_test          INTEGER,
    train_acc       REAL,
    test_acc        REAL,
    imbalance_ratio REAL,
    status          TEXT NOT NULL DEFAULT 'done', -- running / done / failed
    started_at      TEXT DEFAULT (datetime('now')),
    finished_at     TEXT DEFAULT (datetime('now')),
    FOREIGN KEY (agent_id) REFERENCES agents(id)
);

-- 样本库（供数据查看与分布分析）
CREATE TABLE IF NOT EXISTS samples (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    agent_name TEXT NOT NULL,
    f1         REAL NOT NULL,
    f2         REAL NOT NULL,
    y          INTEGER NOT NULL,                -- 标签 0/1
    split      TEXT NOT NULL                    -- train / test
);

-- 系统配置（键值）
CREATE TABLE IF NOT EXISTS config (
    key   TEXT PRIMARY KEY,
    value TEXT
);

-- 审计日志
CREATE TABLE IF NOT EXISTS audit_log (
    id      INTEGER PRIMARY KEY AUTOINCREMENT,
    ts      TEXT DEFAULT (datetime('now')),
    actor   TEXT DEFAULT 'console',
    action  TEXT,
    detail  TEXT
);

-- 默认配置
INSERT OR IGNORE INTO config (key, value) VALUES
    ('n_train', '400'),
    ('n_test', '200'),
    ('fw_rate', '100'),
    ('fw_burst', '10'),
    ('fw_max_payload', '1024');
