# ZhiOS-AF Docker 多阶段构建
#
# 对应《15-Docker容器化构建与部署文档》。
# 阶段1 基础工具链镜像；阶段2 交叉/主机构建；阶段3 运行/测试镜像。
# 默认（host）在容器内完成编译并运行单元测试，方便无缝植入与二次开发。

# ---------- 阶段 1/2：构建（含工具链） ----------
FROM gcc:12 AS build
WORKDIR /app

# 仅复制构建所需（利用缓存）
COPY Makefile .
COPY include ./include
COPY rtos ./rtos
COPY hal ./hal
COPY bsp ./bsp
COPY kernel ./kernel
COPY ai_kernel ./ai_kernel
COPY model_runtime ./model_runtime
COPY capability ./capability
COPY agent ./agent
COPY ai_service ./ai_service
COPY comm ./comm
COPY tools ./tools
COPY tests ./tests
COPY examples ./examples

# 主机构建：单元测试 + 演示
RUN make zhio_tests zhio_demo

# ---------- 阶段 3：运行/测试镜像 ----------
FROM debian:bookworm-slim AS runtime
RUN apt-get update && apt-get install -y --no-install-recommends libc6 && \
    rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY --from=build /app/zhio_tests /app/zhio_tests
COPY --from=build /app/zhio_demo /app/zhio_demo

# 默认执行单元测试（可被覆盖：docker run ... /app/zhio_demo）
CMD ["/app/zhio_tests"]
