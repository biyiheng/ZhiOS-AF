/*
 * tensor_mem.h - 零碎片张量内存管理器
 *
 * 对应《06-张量内存管理器设计文档》《17-API接口规范文档》第 5 节。
 * 双区布局：
 *   - 持久区（Bump 分配器）：模型权重、Agent 长期记忆，整池复位释放。
 *   - 临时区（栈式分配器）：激活值、推理中间结果，LIFO 天然零碎片。
 * 目标：30 天运行碎片率 <1%，分配 <200 CPU 周期。
 */
#ifndef ZHIO_TENSOR_MEM_H
#define ZHIO_TENSOR_MEM_H

#include <stdint.h>
#include "zhios_types.h"
#include "zhios_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化张量池（pool_bytes 为临时区+持久区合计，可传 0 用默认 ZHIO_CFG_TENSOR_POOL_BYTES） */
int  iTensorPoolInit(uint32_t pool_bytes);
void vTensorPoolDeinit(void);

/* ---- 公开 API（对应文档 17 第 5 节） ---- */
TensorHandle_t xAllocTensor(const TensorDesc_t *desc);
void           vFreeTensor(TensorHandle_t tensor);
int            xGetTensorPoolStats(TensorPoolStats_t *stats);
int            xDefragmentPool(uint32_t min_free_bytes);

/* 便捷访问（非公共 API，供模型运行时与推理执行使用） */
void    *vTensorGetData(TensorHandle_t tensor);
uint32_t ulTensorGetSize(TensorHandle_t tensor);
int      iTensorIsValid(TensorHandle_t tensor);

/* ---- 持久区（内部使用：模型权重/长期记忆；Bump 分配，整体复位） ---- */
void    *zhio_persist_alloc(uint32_t size, uint32_t align);
void     zhio_persist_reset(void);
uint32_t zhio_persist_used(void);

/* 内存模型（供自检/测试）：canary 使能时分配前后填充 0xA5/0x5A */
#ifndef ZHIO_CFG_TENSOR_CANARY
#define ZHIO_CFG_TENSOR_CANARY 1
#endif

#ifdef __cplusplus
}
#endif

#endif /* ZHIO_TENSOR_MEM_H */
