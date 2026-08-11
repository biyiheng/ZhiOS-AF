# ZhiOS-AF UART 环形缓冲驱动独立镜像
#
# 对应《14-硬件抽象层-HAL接口规范》。
# 将"修复后的驱动代码"（环形缓冲 + 非阻塞接收，源自 hal_host.c）封装为
# 独立可运行镜像，在容器内验证 FIFO 顺序 / 满丢弃 / 空读 / 回绕。
#
# 构建上下文为仓库根目录（zhi-os-af/）：
#   docker build -f docker/driver/Dockerfile -t zhi-af/uart-driver .
#   docker run --rm zhi-af/uart-driver
FROM gcc:12

WORKDIR /app

# 复制独立驱动模块与测试桩
COPY docker/driver/uart_ringbuf.h /app/uart_ringbuf.h
COPY docker/driver/uart_ringbuf.c /app/uart_ringbuf.c
COPY docker/driver/driver_harness.c /app/driver_harness.c

RUN gcc -std=c99 -Wall -Wextra -o uart_drv_test uart_ringbuf.c driver_harness.c

CMD ["./uart_drv_test"]
