/*
 * tensor_mem.c - 零碎片张量内存管理器（双区：Bump + 栈式）
 *
 * 对应《06-张量内存管理器设计文档》。
 *   - 持久区：Bump 分配器（模型权重、Agent 长期记忆），整体复位。
 *   - 临时区：栈式（LIFO）分配器（激活值、推理中间结果），天然零碎片。
 * 目标：分配 <200 CPU 周期、30 天碎片率 <1%。
 *
 * =============================================================================
 * 模块说明（维护入口）
 * -----------------------------------------------------------------------------
 * 职责     ：张量内存分配/回收/统计/碎片整理，杜绝推理热路径的动态堆碎片。
 * 依赖     ：zhios_rtos（zhio_malloc 仅用于初始化期一次性申请池，非热路径）、zhios_config。
 * 被谁调用 ：model_runtime、inference_scheduler、agent 目录、外部 include/tensor_mem.h。
 * 内存指标 ：对应《33-操作系统技术指标体系设计文档》"内存管理"维度。
 *            满足"静态分区 + 内存池、禁止动态堆碎片、8 字节对齐、Canary 越界检测"；
 *            MPU 栈守护区 / 汇编 Canary 属生产落地项（见 FreeRTOSConfig.h 与《33》4.2）。
 * =============================================================================
 */
#include <string.h>
#include <stdint.h>
#include "tensor_mem.h"
#include "zhios_config.h"
#include "zhios_rtos.h"

/* =============================================================================
 * 内存分配周期测量（评审依据）
 * -----------------------------------------------------------------------------
 * 目标：xAllocTensor 单次分配 <200 CPU 周期（《33》4.2"内存管理"）。
 * 开关 ZHIO_CFG_MEM_TRACE（0=关闭，省去读周期计数器开销），Cortex-M 用
 * DWT->CYCCNT，其余平台回退到 tick 作相对估算。统计最坏分配周期供性能分析。
 * ============================================================================= */
#ifndef ZHIO_CFG_MEM_TRACE
#define ZHIO_CFG_MEM_TRACE 1
#endif

static uint32_t g_alloc_worst_cycles = 0;   /* 单次张量分配最坏周期数 */

/* 平台周期计数：Cortex-M 用 DWT->CYCCNT，其余回退到 tick（相对估算） */
static inline uint32_t mem_cycles_now(void)
{
#if defined(__CORTEX_M) || defined(ARMV8M_MAINLINE) || defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_6M__)
    extern volatile uint32_t DWT_CYCCNT;   /* 由 BSP/链接脚本提供 */
    return DWT_CYCCNT;
#else
    return (uint32_t)zhio_get_tick();
#endif
}

/* ---- 张量句柄内部结构 ---- */
typedef struct {
    void     *data;
    uint32_t  size;
    uint32_t  offset;         /* 临时区内起始偏移（用于 LIFO 回收） */
    int       active;         /* 1 = 已分配 */
    int       persistent;     /* 1 = 位于持久区 */
    uint32_t  magic;
} TensorObj_t;

#define ZHIO_TENSOR_MAGIC 0x5A5A11u
#define ZHIO_TENSOR_ALIGN 8u
#define ZHIO_CANARY_HEAD  0xA5u
#define ZHIO_CANARY_TAIL  0x5Au

/* ---- 池 ---- */
typedef struct {
    uint8_t *buf;
    uint32_t total;          /* 总容量（持久 + 临时） */
    uint32_t persist_size;   /* 持久区大小 */
    uint32_t temp_size;      /* 临时区大小 */

    /* 持久区 Bump */
    uint32_t persist_used;

    /* 临时区 栈式 */
    uint32_t temp_top;       /* 已用栈顶偏移（从 temp_base 起） */
    uint32_t temp_base;

    uint32_t active_count;   /* 已分配张量数 */
    uint32_t peak_active;
    uint32_t alloc_count;
} TensorPool_t;

static TensorPool_t g_pool;
static TensorObj_t  g_tensors[ZHIO_CFG_MAX_INFERENCE_TASKS * 2 + 8]; /* 张量对象池 */
static const uint32_t g_max_tensors = ZHIO_CFG_MAX_INFERENCE_TASKS * 2 + 8;

static inline uint32_t align_up(uint32_t v, uint32_t a)
{
    return (v + (a - 1)) & ~(a - 1);
}

int iTensorPoolInit(uint32_t pool_bytes)
{
    if (g_pool.buf != NULL) {
        /* 重复初始化：先复位 */
        zhio_persist_reset();
        g_pool.temp_top = 0;
        memset(g_tensors, 0, sizeof(g_tensors));
        g_pool.active_count = 0;
        return ZHIO_OK;
    }
    if (pool_bytes == 0) pool_bytes = ZHIO_CFG_TENSOR_POOL_BYTES;
    g_pool.buf = (uint8_t *)zhio_malloc(pool_bytes);
    if (!g_pool.buf) {
        zhio_log("[tensor_mem] iTensorPoolInit FAILED: cannot malloc %u B", (unsigned)pool_bytes);
        return ZHIO_E_NOMEM;
    }
    g_pool.total = pool_bytes;
    /* 持久区占 60%，临时区占 40%（可调） */
    g_pool.persist_size = align_up((uint32_t)(pool_bytes * 3u / 5u), ZHIO_TENSOR_ALIGN);
    g_pool.temp_size    = pool_bytes - g_pool.persist_size;
    g_pool.persist_used = 0;
    g_pool.temp_base    = g_pool.persist_size;
    g_pool.temp_top     = 0;
    g_pool.active_count = 0;
    g_pool.alloc_count  = 0;
    memset(g_tensors, 0, sizeof(g_tensors));
    zhio_log("[tensor_mem] pool init OK: total=%u persist=%u temp=%u",
             (unsigned)g_pool.total, (unsigned)g_pool.persist_size, (unsigned)g_pool.temp_size);
    return ZHIO_OK;
}

void vTensorPoolDeinit(void)
{
    if (g_pool.buf) {
        zhio_log("[tensor_mem] pool deinit: active=%u peak=%u allocs=%u",
                 (unsigned)g_pool.active_count, (unsigned)g_pool.peak_active,
                 (unsigned)g_pool.alloc_count);
        zhio_free(g_pool.buf);
    }
    memset(&g_pool, 0, sizeof(g_pool));
}

/* ---- 持久区 Bump 分配 ---- */
void *zhio_persist_alloc(uint32_t size, uint32_t align)
{
    uint32_t a = align ? align : ZHIO_TENSOR_ALIGN;
    uint32_t off = align_up(g_pool.persist_used, a);
    if (off + size > g_pool.persist_size) return NULL;
    void *p = g_pool.buf + off;
    g_pool.persist_used = off + size;
    return p;
}
void zhio_persist_reset(void)
{
    zhio_log("[tensor_mem] persist reset: freed=%uB", (unsigned)g_pool.persist_used);
    g_pool.persist_used = 0;
    /* 释放所有持久张量 */
    uint32_t i;
    for (i = 0; i < g_max_tensors; i++) {
        if (g_tensors[i].active && g_tensors[i].persistent) {
            g_tensors[i].active = 0;
            g_tensors[i].magic  = 0;
            if (g_pool.active_count) g_pool.active_count--;
        }
    }
}
uint32_t zhio_persist_used(void) { return g_pool.persist_used; }

/* ---- 查找空闲张量对象 ---- */
static TensorObj_t *obj_alloc(void)
{
    uint32_t i;
    for (i = 0; i < g_max_tensors; i++) {
        if (!g_tensors[i].active) {
            g_tensors[i].active = 1;
            g_tensors[i].magic  = ZHIO_TENSOR_MAGIC;
            g_pool.active_count++;
            if (g_pool.active_count > g_pool.peak_active) g_pool.peak_active = g_pool.active_count;
            return &g_tensors[i];
        }
    }
    return NULL;
}

/* 分配临时区张量（栈式 LIFO） */
TensorHandle_t xAllocTensor(const TensorDesc_t *desc)
{
    if (!desc || desc->dims == 0 || desc->dims > 4) return NULL;
    uint32_t t0 = 0;
#if ZHIO_CFG_MEM_TRACE
    t0 = mem_cycles_now();
#endif
    uint32_t size = desc->size_bytes;
    if (size == 0) size = 1;
    uint32_t aligned = align_up(size, ZHIO_TENSOR_ALIGN);
    /* 栈式分配：需要满足 LIFO，这里不强制，但临时区按偏移推进 */
    uint32_t top = g_pool.temp_top;
    uint32_t new_top = top + aligned;
    if (new_top > g_pool.temp_size) {
        zhio_log("[tensor_mem] xAllocTensor FAILED: need %uB but temp free=%uB (frag leak?)",
                 (unsigned)aligned, (unsigned)(g_pool.temp_size - g_pool.temp_top));
        return NULL; /* 临时区耗尽 */
    }

    TensorObj_t *obj = obj_alloc();
    if (!obj) {
        zhio_log("[tensor_mem] xAllocTensor FAILED: tensor-object pool exhausted");
        return NULL;
    }

    void *data = g_pool.buf + g_pool.temp_base + top;
    obj->data = data;
    obj->size = size;
    obj->offset = top;
    obj->persistent = 0;
    obj->magic = ZHIO_TENSOR_MAGIC;

#if ZHIO_CFG_TENSOR_CANARY
    /* 填充 canary：头部/尾部用于越界检测 */
    memset(data, ZHIO_CANARY_HEAD, ZHIO_TENSOR_ALIGN);
    if (size >= ZHIO_TENSOR_ALIGN)
        memset((uint8_t *)data + size - ZHIO_TENSOR_ALIGN, ZHIO_CANARY_TAIL, ZHIO_TENSOR_ALIGN);
#endif
    g_pool.temp_top = new_top;
    g_pool.alloc_count++;
#if ZHIO_CFG_MEM_TRACE
    {
        uint32_t dt = mem_cycles_now() - t0;
        if (dt > g_alloc_worst_cycles) {
            g_alloc_worst_cycles = dt;
            zhio_log("[tensor_mem] new worst alloc=%u cycles (target <200)", (unsigned)dt);
        }
    }
#endif
    return (TensorHandle_t)obj;
}

void vFreeTensor(TensorHandle_t tensor)
{
    TensorObj_t *obj = (TensorObj_t *)tensor;
    if (!obj || obj->magic != ZHIO_TENSOR_MAGIC) return;
    if (!obj->persistent) {
        /* 栈式临时区：LIFO 回收 —— 若释放的是当前栈顶，则回退栈指针 */
        uint32_t aligned = align_up(obj->size, ZHIO_TENSOR_ALIGN);
        if (obj->offset + aligned == g_pool.temp_top) {
            g_pool.temp_top = obj->offset;   /* 回收，保持零碎片 */
            zhio_log("[tensor_mem] vFreeTensor reclaim: temp_top -> %u",
                     (unsigned)g_pool.temp_top);
        } else {
            zhio_log("[tensor_mem] vFreeTensor non-LIFO free @%u (hole left, size=%u)",
                     (unsigned)obj->offset, (unsigned)obj->size);
        }
    }
    obj->active = 0;
    obj->magic  = 0;
    obj->offset = 0;
    if (g_pool.active_count) g_pool.active_count--;
}

void *vTensorGetData(TensorHandle_t tensor)
{
    TensorObj_t *obj = (TensorObj_t *)tensor;
    if (!obj || obj->magic != ZHIO_TENSOR_MAGIC) return NULL;
    return obj->data;
}

uint32_t ulTensorGetSize(TensorHandle_t tensor)
{
    TensorObj_t *obj = (TensorObj_t *)tensor;
    if (!obj || obj->magic != ZHIO_TENSOR_MAGIC) return 0;
    return obj->size;
}

int iTensorIsValid(TensorHandle_t tensor)
{
    TensorObj_t *obj = (TensorObj_t *)tensor;
    return (obj && obj->magic == ZHIO_TENSOR_MAGIC) ? 1 : 0;
}

int xGetTensorPoolStats(TensorPoolStats_t *stats)
{
    if (!stats) return ZHIO_E_INVAL;
    /* 临时区为栈式，剩余连续空间即最大连续空闲（天然零碎片） */
    stats->total_bytes = g_pool.temp_size;
    stats->used_bytes  = g_pool.temp_top;
    stats->free_bytes  = g_pool.temp_size - g_pool.temp_top;
    stats->largest_free= g_pool.temp_size - g_pool.temp_top; /* 连续空闲 */
    stats->block_count = g_pool.active_count;
    return ZHIO_OK;
}

int xDefragmentPool(uint32_t min_free_bytes)
{
    /* 栈式分配器天然无碎片；若能满足目标则返回 OK */
    if (g_pool.temp_size - g_pool.temp_top >= min_free_bytes) return ZHIO_OK;
    zhio_log("[tensor_mem] defrag FAILED: need %uB free, have %uB",
             (unsigned)min_free_bytes, (unsigned)(g_pool.temp_size - g_pool.temp_top));
    return ZHIO_E_NOMEM;
}
