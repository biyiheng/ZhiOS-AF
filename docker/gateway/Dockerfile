# ZhiOS-AF 云端 AI 服务网关镜像
#
# 对应《15-Docker容器化构建与部署文档》。
# 纯 Python 标准库实现，无需额外依赖，便于快速部署。
# 密钥通过运行环境注入（OPENAI_API_KEY / CLAUDE_API_KEY），不写入镜像。
FROM python:3.11-slim

WORKDIR /gateway
COPY gateway.py /gateway/gateway.py

EXPOSE 8080
ENV GATEWAY_PORT=8080 GATEWAY_MOCK=1

CMD ["python", "/gateway/gateway.py"]
