# ZhiOS-AF Agent 控制软件 · 容器端到端综合验证镜像
#
# 对应《agent控制软件技术文档.md》
# 在单个容器内依次验证：
#   1) Agent 控制软件端到端（前端 + 后端 + SQLite + 全功能模块，e2e_test.py）
#   2) C 层防火墙逻辑（firewall.c + fw_harness.c）
#   3) 驱动接口（UART 环形缓冲，uart_ringbuf + driver_harness）
#
# 构建上下文必须是仓库根目录（zhi-os-af/）：
#   docker build -f docker/e2e/Dockerfile -t zhi-af/e2e .
#   docker run --rm zhi-af/e2e
# =============================================================================
# Stage 1：编译 C 防火墙与驱动测试桩（gcc）
FROM gcc:12 AS build
WORKDIR /app

# --- 防火墙 ---
COPY ai_kernel/security/firewall.c /app/firewall.c
COPY include/firewall.h /app/include/firewall.h
COPY include/zhios_err.h /app/include/zhios_err.h
COPY include/zhios_config.h /app/include/zhios_config.h
COPY include/zhios_rtos.h /app/include/zhios_rtos.h
COPY docker/security/fw_harness.c /app/fw_harness.c
RUN gcc -std=c99 -Wall -Wextra -Iinclude -o fw_test fw_harness.c firewall.c

# --- 驱动 ---
COPY docker/driver/uart_ringbuf.h /app/uart_ringbuf.h
COPY docker/driver/uart_ringbuf.c /app/uart_ringbuf.c
COPY docker/driver/driver_harness.c /app/driver_harness.c
RUN gcc -std=c99 -Wall -Wextra -o uart_drv_test uart_ringbuf.c driver_harness.c

# =============================================================================
# Stage 2：运行端到端综合验证（python + 编译产物）
FROM python:3.11-slim
WORKDIR /app

# Agent 控制软件源码（app / trainer / schema / migrate / 前端）
COPY tools/agent_console/ /app/

# C 编译产物（glibc 兼容）
COPY --from=build /app/fw_test /app/fw_test
COPY --from=build /app/uart_drv_test /app/uart_drv_test

# 端到端入口
COPY docker/e2e/run_e2e.sh /app/run_e2e.sh
RUN chmod +x /app/run_e2e.sh

CMD ["/app/run_e2e.sh"]
