# ZhiOS-AF 顶层 Makefile（主机快速构建）
#
# 用法：
#   make            # 构建 zhio_tests 与 zhio_demo（host 平台）
#   make test       # 构建并运行单元测试
#   make demo       # 构建并运行演示
#   make clean
# 交叉编译（MCU）请使用 CMake + 工具链文件（见 16-交叉编译文档）。

CC      ?= cc
CFLAGS  ?= -std=c99 -O2 -Wall -Wextra -Wno-unused-parameter \
	-Iinclude -Iagent/sub_agents -Iai_service/cloud -Iai_service/cloud/adapters -Itools/ztest -pthread
LDFLAGS ?= -pthread

CORE_SRCS = \
	kernel/kernel.c \
	ai_kernel/tensor_mem/tensor_mem.c \
	ai_kernel/npu_dsp/npu_dsp.c \
	ai_kernel/inference_scheduler/inference_scheduler.c \
	ai_kernel/security/security.c \
	ai_kernel/security/firewall.c \
	model_runtime/model_runtime.c \
	capability/capability.c \
	agent/message_bus/message_bus.c \
	agent/auto_agent/agent.c \
	agent/sub_agents/sub_agents.c \
	ai_service/ai_service.c \
	ai_service/cloud/cloud_transport.c \
	ai_service/cloud/adapters/openai/openai_adapter.c \
	ai_service/cloud/adapters/claude/claude_adapter.c \
	ai_service/cloud/adapters/a2a/a2a_adapter.c \
	ai_service/cloud/adapters/mcp/mcp_adapter.c \
	comm/comm.c \
	tools/ztest/ztest.c \
	rtos/host/zhio_rtos_port.c \
	hal/hal_host.c \
	bsp/host/board.c

CORE_OBJS = $(CORE_SRCS:.c=.o)

.PHONY: all test demo clean

all: zhio_tests zhio_demo

zhio_tests: $(CORE_OBJS) tests/test_main.o tests/test_core.o tests/test_agent.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

zhio_demo: $(CORE_OBJS) examples/demo_main.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

test: zhio_tests
	./zhio_tests

demo: zhio_demo
	./zhio_demo

clean:
	rm -f $(CORE_OBJS) tests/*.o examples/*.o zhio_tests zhio_demo
