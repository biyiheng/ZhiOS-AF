# ZhiOS-AF 训练链路 · Docker 隔离环境自动测试镜像
#
# 对应《agent控制软件技术文档.md》《22-压力测试方案》。
# 在纯净 python:3.11-slim 容器中运行"合法爬虫取数 + 口袋感知机训练 + 评估"，
# 验证环境隔离下的执行结果（仅标准库，无第三方 ML 依赖）。
#
# 构建： docker build -f docker/sim/Dockerfile -t zhi-af/sim-training .
# 运行： docker run --rm zhi-af/sim-training
FROM python:3.11-slim

WORKDIR /app

COPY tools/agent_console/trainer.py /app/trainer.py
COPY docker/sim/training_test.py /app/training_test.py

# 可选：通过环境变量调节样本量
ENV N_TRAIN=400 N_TEST=200

CMD ["python", "/app/training_test.py"]
