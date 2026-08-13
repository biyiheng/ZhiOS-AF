# ZhiOS-AF Agent 控制软件镜像（纯 Python 标准库，零第三方依赖）
#
# 对应《agent控制软件技术文档.md》
# 提供：前端控制台 + 后端 REST API + SQLite 数据库 + 训练引擎。
#
# 构建：  docker build -f docker/agent_console/Dockerfile -t zhi-af/agent-console .
# 运行：  docker run --rm -p 8000:8000 -v agent_data:/data zhi-af/agent-console --seed-agents
FROM python:3.11-slim

WORKDIR /app

COPY tools/agent_console/ /app/

# 数据库与样本落盘到 /data，便于持久化与二次开发
ENV AGENT_CONSOLE_DB=/data/agent_console.db
VOLUME ["/data"]

EXPOSE 8000
CMD ["python", "/app/app.py", "--port", "8000", "--db", "/data/agent_console.db", "--seed-agents"]
