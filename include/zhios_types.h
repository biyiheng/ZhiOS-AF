/*
 * zhios_types.h - ZhiOS-AF 公共数据类型
 *
 * 对应《17-API接口规范文档》第 2 节：句柄、张量描述、池统计。
 */
#ifndef ZHIO_TYPES_H
#define ZHIO_TYPES_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 句柄类型（统一 void*，保持 ABI 稳定） ---- */
typedef void *ModelHandle_t;
typedef void *TensorHandle_t;
typedef void *AgentHandle_t;
typedef void *CloudAdapterHandle_t;
typedef void *SafetyValidatorHandle_t;

/* ---- 张量元素类型 ---- */
typedef enum {
    ZHIO_TENSOR_F32   = 0,
    ZHIO_TENSOR_INT8  = 1,    /* CMSIS-NN/TFLM int8 量化 */
    ZHIO_TENSOR_UINT8 = 2,
    ZHIO_TENSOR_INT16 = 3,
    ZHIO_TENSOR_INT32 = 4,
} zhios_tensor_type_t;

/* ---- 张量描述 ---- */
typedef struct {
    uint8_t  dims;            /* 维度数量 (1-4) */
    uint32_t shape[4];        /* 各维度大小 */
    int32_t  type;            /* 元素类型枚举 zhios_tensor_type_t */
    uint32_t size_bytes;      /* 总字节数 */
} TensorDesc_t;

/* ---- 张量池统计 ---- */
typedef struct {
    uint32_t total_bytes;     /* 池总容量 */
    uint32_t used_bytes;      /* 已用字节 */
    uint32_t free_bytes;      /* 空闲字节 */
    uint32_t largest_free;    /* 最大连续空闲块（碎片化指标） */
    uint32_t block_count;     /* 当前块数 */
} TensorPoolStats_t;

/* ---- 推理任务状态 ---- */
typedef enum {
    ZHIO_ITASK_CREATED = 0,
    ZHIO_ITASK_READY,
    ZHIO_ITASK_RUNNING,
    ZHIO_ITASK_SUSPENDED,
    ZHIO_ITASK_COMPLETED,
    ZHIO_ITASK_ERROR,
} InferenceTaskState_t;

/* ---- 模型状态（模型即对象状态机） ---- */
typedef enum {
    ZHIO_MODEL_UNLOADED  = 0,
    ZHIO_MODEL_LOADING,
    ZHIO_MODEL_READY,
    ZHIO_MODEL_RUNNING,
    ZHIO_MODEL_ERROR_RECOVERABLE,
    ZHIO_MODEL_ERROR_FATAL,
} ModelState_t;

/* ---- 模型格式 ---- */
typedef enum {
    ZHIO_MODEL_FMT_TFLM = 0,   /* TensorFlow Lite Micro */
    ZHIO_MODEL_FMT_RAW,        /* 自定义原始权重 */
    ZHIO_MODEL_FMT_HOST_SIM,   /* 主机仿真模型 */
} ModelFormat_t;

/* ---- Agent 状态机 ---- */
typedef enum {
    ZHIO_AGENT_IDLE = 0,
    ZHIO_AGENT_CONFIGURING,
    ZHIO_AGENT_READY,
    ZHIO_AGENT_EXECUTING,
    ZHIO_AGENT_OFFLOADING,
    ZHIO_AGENT_FUSING,
    ZHIO_AGENT_COMPLETED,
    ZHIO_AGENT_ERROR,
} AgentState_t;

/* ---- 消息类型 ---- */
typedef enum {
    ZHIO_MSG_REQUEST   = 0,
    ZHIO_MSG_RESPONSE,
    ZHIO_MSG_EVENT,
    ZHIO_MSG_BROADCAST,
} AgentMessageType_t;

/* ---- 路由模式 ---- */
typedef enum {
    ZHIO_ROUTE_LOCAL_ONLY  = 0,   /* 仅本地（断网/隐私场景） */
    ZHIO_ROUTE_LOCAL_FIRST,       /* 本地优先 */
    ZHIO_ROUTE_CLOUD_FIRST,       /* 云端优先 */
    ZHIO_ROUTE_CLOUD_ONLY,        /* 仅云端 */
    ZHIO_ROUTE_AUTO,              /* 自动（综合延迟/精度/成本） */
} RoutingMode_t;

/* ---- 安全等级 ---- */
typedef enum {
    ZHIO_SAFETY_LOW      = 0,
    ZHIO_SAFETY_MEDIUM   = 1,
    ZHIO_SAFETY_HIGH     = 2,
    ZHIO_SAFETY_CRITICAL = 3,
} SafetyLevel_t;

/* ---- 张量元素字节（按类型） ---- */
static inline uint32_t zhio_tensor_elem_bytes(int32_t type)
{
    switch (type) {
        case ZHIO_TENSOR_F32:   return 4;
        case ZHIO_TENSOR_INT16: return 2;
        case ZHIO_TENSOR_INT8:  return 1;
        case ZHIO_TENSOR_UINT8: return 1;
        case ZHIO_TENSOR_INT32: return 4;
        default:                return 1;
    }
}

#ifdef __cplusplus
}
#endif

#endif /* ZHIO_TYPES_H */
