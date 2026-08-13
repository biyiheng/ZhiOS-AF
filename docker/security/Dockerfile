# ZhiOS-AF 通信防火墙独立镜像
#
# 对应《20-安全性设计文档》《21-威胁模型与风险评估文档》。
# 将"修复后的防火墙逻辑"（firewall.c）封装为独立可运行镜像：
#   编译防火墙源码 + 独立测试桩（fw_harness.c），在容器内验证
#   默认拒绝 / 白名单 / 源白名单 / 长度上限 / 令牌桶限速。
#
# 构建上下文必须是仓库根目录（zhi-os-af/）：
#   docker build -f docker/security/Dockerfile -t zhi-af/firewall .
#   docker run --rm zhi-af/firewall
FROM gcc:12

WORKDIR /app

# 复制防火墙实现与所需头文件
COPY ai_kernel/security/firewall.c /app/firewall.c
COPY include/firewall.h /app/include/firewall.h
COPY include/zhios_err.h /app/include/zhios_err.h
COPY include/zhios_config.h /app/include/zhios_config.h
COPY include/zhios_rtos.h /app/include/zhios_rtos.h

# 复制独立测试桩
COPY docker/security/fw_harness.c /app/fw_harness.c

RUN gcc -std=c99 -Wall -Wextra -Iinclude -o fw_test fw_harness.c firewall.c

CMD ["./fw_test"]
