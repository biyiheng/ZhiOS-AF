/*
 * zhios_config.h - ZhiOS-AF 编译期配置（Kconfig 风格）
 *
 * ZhiOS-AF 智脑实时操作系统 Agent 框架版
 * 基于 FreeRTOS V11.2.0+ 二次开发，Agent-First 嵌入式 RTOS
 * License: Apache 2.0
 *
 * 本文件是 ZhiOS-AF 的统一配置入口，通过宏裁剪组件与容量，
 * 对应《16-交叉编译与工具链配置文档》中 FreeRTOSConfig.h 的配置映射。
 */
#ifndef ZHIO_CONFIG_H
#define ZHIO_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* ============ 版本 ============ */
#define ZHIO_VERSION_MAJOR   1
#define ZHIO_VERSION_MINOR   0
#define ZHIO_VERSION_PATCH   0
#define ZHIO_VERSION_STRING  "1.0.0"

/* ============ 组件裁剪（1 开启 / 0 关闭） ============ */
#define ZHIO_CFG_INFERENCE_SCHEDULER   1  /* 推理任务调度器 */
#define ZHIO_CFG_TENSOR_MEM            1  /* 张量内存管理器 */
#define ZHIO_CFG_MODEL_RUNTIME         1  /* 模型运行时 */
#define ZHIO_CFG_NPU_DSP               1  /* NPU/DSP HAL */
#define ZHIO_CFG_SECURITY              1  /* 安全子模块 */
#define ZHIO_CFG_AGENT                 1  /* Agent 自治框架 */
#define ZHIO_CFG_CAPABILITY            1  /* 能力偏好引擎 */
#define ZHIO_CFG_AI_SERVICE            1  /* 混合 AI 服务层 */
#define ZHIO_CFG_COMM                  1  /* 通信协议层 */

/* ============ 低内存配置档案 ============
 * 在资源受限（低 RAM/低端 MCU）环境下，开启 ZHIO_CFG_LOW_MEMORY 可自动把
 * 各容量上限与张量池压缩到保守值，换取更低的内存占用与更稳定的运行；
 * 代价是并发能力下降（任务/Agent/队列/异步推理数减少）。默认关闭（全功能）。
 */
#ifndef ZHIO_CFG_LOW_MEMORY
#define ZHIO_CFG_LOW_MEMORY  0
#endif

/* ============ 容量上限（对应技术规格可扩展上限） ============ */
#if ZHIO_CFG_LOW_MEMORY
  /* 低内存档案：压缩容量，降低静态数组/队列内存占用 */
  #define ZHIO_CFG_MAX_INFERENCE_TASKS   8       /* 推理任务数（低配） */
  #define ZHIO_CFG_MAX_AGENTS            4       /* 子 Agent 数（低配） */
  #define ZHIO_CFG_AGENT_QUEUE_DEPTH     64      /* Agent 消息队列深度（低配） */
  #define ZHIO_CFG_MAX_ASYNC_INFERENCE   2       /* 异步推理并发上限（低配） */
#else
  #define ZHIO_CFG_MAX_INFERENCE_TASKS   32      /* 推理任务数 */
  #define ZHIO_CFG_MAX_AGENTS            8       /* 子 Agent 数 */
  #define ZHIO_CFG_AGENT_QUEUE_DEPTH     256     /* Agent 消息队列深度 */
  #define ZHIO_CFG_MAX_ASYNC_INFERENCE   8       /* 异步推理并发上限 */
#endif
#define ZHIO_CFG_MAX_LOCAL_MODELS      8       /* 本地模型数 */
#define ZHIO_CFG_MAX_CLOUD_ADAPTERS    4       /* 云端适配器数 */
#define ZHIO_CFG_MAX_KEYWORDS          64      /* 最大关键词数 */
#define ZHIO_CFG_KEYWORD_LEN           32      /* 单个关键词最大长度 */
#define ZHIO_CFG_MAX_MSG_SIZE          256     /* Agent 消息体最大字节 */

/* ============ 栈尺寸（字节，默认；可被调用方覆盖） ============ */
#ifndef ZHIO_CFG_AUTO_AGENT_STACK
#define ZHIO_CFG_AUTO_AGENT_STACK  2048    /* Auto 主 Agent 栈 */
#endif
#ifndef ZHIO_CFG_SUBAGENT_STACK
#define ZHIO_CFG_SUBAGENT_STACK    1024    /* 子 Agent 栈 */
#endif

/* ============ 性能目标（tick 值，编译期默认） ============ */
#define ZHIO_CFG_AGENT_DECISION_MS     50      /* Agent 决策目标 <50ms */
#define ZHIO_CFG_SWITCH_CLOUD_MS       200     /* 本地/云端切换 <200ms */
#define ZHIO_CFG_HOT_SWAP_MS           100     /* 模型热切换 <100ms */

/* ============ 张量内存池容量（字节，Bump+栈式双区） ============ */
#if ZHIO_CFG_LOW_MEMORY
#define ZHIO_CFG_TENSOR_POOL_BYTES     (32U * 1024U)   /* 低配 32KB */
#else
#define ZHIO_CFG_TENSOR_POOL_BYTES     (96U * 1024U)   /* 默认 96KB，可在 BSP 中覆盖 */
#endif

/* ============ 模型后端选择（运行时后端） ============ */
#define ZHIO_CFG_BACKEND_NONE        0
#define ZHIO_CFG_BACKEND_HOST_SIM    1   /* 主机仿真后端（无需 TFLM，用于单元测试/演示） */
#define ZHIO_CFG_BACKEND_CMSIS_NN    2   /* CMSIS-NN 软件加速 */
#define ZHIO_CFG_BACKEND_ETHOS_U     3   /* Ethos-U NPU */
#define ZHIO_CFG_BACKEND_EIQ_NEUTRON 4   /* NXP eIQ Neutron */
#ifndef ZHIO_CFG_DEFAULT_BACKEND
#define ZHIO_CFG_DEFAULT_BACKEND     ZHIO_CFG_BACKEND_HOST_SIM
#endif

#ifdef __cplusplus
}
#endif

#endif /* ZHIO_CONFIG_H */
