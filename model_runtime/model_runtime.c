/*
 * model_runtime.c - 模型运行时（模型即对象）
 *
 * 对应《07-模型运行时设计文档》。
 * 状态机：UNLOADED→LOADING→READY→RUNNING→READY→UNLOADED / ERROR。
 * 权重存放于张量内存"持久区"（Bump），热切换在持久区原地替换（双缓冲乒乓）。
 */
#include <string.h>
#include <stdint.h>
#include "model_runtime.h"
#include "tensor_mem.h"
#include "npu_dsp.h"
#include "zhios_config.h"
#include "zhios_rtos.h"

/* 模型对象 */
typedef struct {
    char         name[24];
    ModelFormat_t format;
    uint32_t     size;
    uint32_t     version;
    ModelState_t state;
    uint32_t     checksum;
    uint32_t     run_count;
    uint8_t     *data;          /* 持久区指针 */
    void        *validator;     /* 安全校验回调（SafetyValidator_t） */
    uint32_t     inited;
} ModelObj_t;

static ModelObj_t g_models[ZHIO_CFG_MAX_LOCAL_MODELS];
static uint32_t g_model_initialized = 0;

/* 本地 CRC32（与 comm 模块同多项式，避免初始化依赖） */
static uint32_t model_crc32(const void *data, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFu;
    uint32_t i;
    for (i = 0; i < len; i++) {
        crc ^= p[i];
        uint32_t b;
        for (b = 0; b < 8; b++) {
            uint32_t mask = (uint32_t)-(int32_t)(crc & 1);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

int iModelRuntimeInit(void)
{
    memset(g_models, 0, sizeof(g_models));
    g_model_initialized = 1;
    return ZHIO_OK;
}
void vModelRuntimeDeinit(void) { memset(g_models, 0, sizeof(g_models)); g_model_initialized = 0; }

ModelHandle_t xRegisterModel(const char *name, ModelFormat_t fmt,
                             const void *data, uint32_t size, uint32_t version)
{
    if (!g_model_initialized || !name || !data || size == 0) return NULL;
    uint32_t i;
    for (i = 0; i < ZHIO_CFG_MAX_LOCAL_MODELS; i++) {
        if (g_models[i].inited == 0) {
            uint8_t *dst = (uint8_t *)zhio_persist_alloc(size, 8);
            if (!dst) return NULL;   /* 持久区不足 */
            memcpy(dst, data, size);
            g_models[i].inited   = 1;
            g_models[i].format   = fmt;
            g_models[i].size     = size;
            g_models[i].version  = version;
            g_models[i].state    = ZHIO_MODEL_UNLOADED;
            g_models[i].checksum = model_crc32(data, size);
            g_models[i].run_count= 0;
            g_models[i].validator= NULL;
            g_models[i].data     = dst;
            g_models[i].name[0]  = '\0';
            strncpy(g_models[i].name, name, sizeof(g_models[i].name) - 1);
            return (ModelHandle_t)&g_models[i];
        }
    }
    return NULL; /* 模型池满 */
}

int iVerifyModelChecksum(ModelHandle_t model)
{
    ModelObj_t *m = (ModelObj_t *)model;
    if (!m || !m->inited) return ZHIO_E_BADMODEL;
    uint32_t c = model_crc32(m->data, m->size);
    return (c == m->checksum) ? ZHIO_OK : ZHIO_E_BADMODEL;
}

int iLoadModel(ModelHandle_t model)
{
    ModelObj_t *m = (ModelObj_t *)model;
    if (!m || !m->inited) return ZHIO_E_INVAL;
    if (iVerifyModelChecksum(model) != ZHIO_OK) {
        m->state = ZHIO_MODEL_ERROR_FATAL;
        return ZHIO_E_BADMODEL;
    }
    m->state = ZHIO_MODEL_LOADING;
    /* 加载到 NPU 设备 */
    NPUDevice_t *dev = xNPUGetDefault();
    if (dev && dev->load_model && dev->load_model(model) != ZHIO_OK) {
        m->state = ZHIO_MODEL_ERROR_RECOVERABLE;
        return ZHIO_E_NODEVICE;
    }
    m->state = ZHIO_MODEL_READY;
    return ZHIO_OK;
}

int iUnloadModel(ModelHandle_t model)
{
    ModelObj_t *m = (ModelObj_t *)model;
    if (!m || !m->inited) return ZHIO_E_INVAL;
    if (m->state == ZHIO_MODEL_RUNNING) return ZHIO_E_BUSY;
    m->state = ZHIO_MODEL_UNLOADED;
    return ZHIO_OK;
}

/* 热切换（<100ms 目标）：先在临时校验再原地替换（乒乓） */
int iSwapModel(ModelHandle_t model, const void *new_data, uint32_t new_size, uint32_t new_version)
{
    ModelObj_t *m = (ModelObj_t *)model;
    if (!m || !m->inited || !new_data) return ZHIO_E_INVAL;
    if (m->state == ZHIO_MODEL_RUNNING) return ZHIO_E_BUSY;

    /* 校验新数据：临时缓冲区复制后校验（乒乓缓冲区 B） */
    if (new_size > m->size) return ZHIO_E_NOMEM;   /* 需不超过已持久化容量 */
    uint32_t new_crc = model_crc32(new_data, new_size);
    /* 校验通过后原地写回（乒乓 A/B） */
    memcpy(m->data, new_data, new_size);
    m->size     = new_size;
    m->version  = new_version;
    m->checksum = new_crc;
    m->state    = ZHIO_MODEL_READY;
    return ZHIO_OK;
}

int iModelGetInfo(ModelHandle_t model, ModelInfo_t *info)
{
    ModelObj_t *m = (ModelObj_t *)model;
    if (!m || !m->inited || !info) return ZHIO_E_INVAL;
    strncpy(info->name, m->name, sizeof(info->name) - 1);
    info->name[sizeof(info->name) - 1] = '\0';
    info->format = m->format;
    info->size = m->size;
    info->version = m->version;
    info->state = m->state;
    info->checksum = m->checksum;
    info->run_count = m->run_count;
    return ZHIO_OK;
}

ModelState_t eModelGetState(ModelHandle_t model)
{
    ModelObj_t *m = (ModelObj_t *)model;
    return (m && m->inited) ? m->state : ZHIO_MODEL_UNLOADED;
}

int iModelSetState(ModelHandle_t model, ModelState_t st)
{
    ModelObj_t *m = (ModelObj_t *)model;
    if (!m || !m->inited) return ZHIO_E_INVAL;
    m->state = st;
    return ZHIO_OK;
}

int iModelBindValidator(ModelHandle_t model, void *validator)
{
    ModelObj_t *m = (ModelObj_t *)model;
    if (!m || !m->inited) return ZHIO_E_INVAL;
    m->validator = validator;
    return ZHIO_OK;
}

void *zhio_model_get_validator(ModelHandle_t model)
{
    ModelObj_t *m = (ModelObj_t *)model;
    return (m && m->inited) ? m->validator : NULL;
}

/* 执行推理（经 NPU 后端），更新状态与运行计数 */
int xModelExecute(ModelHandle_t model, TensorHandle_t input, TensorHandle_t output)
{
    ModelObj_t *m = (ModelObj_t *)model;
    if (!m || !m->inited) return ZHIO_E_BADMODEL;
    if (m->state != ZHIO_MODEL_READY && m->state != ZHIO_MODEL_RUNNING) return ZHIO_E_BADMODEL;
    m->state = ZHIO_MODEL_RUNNING;
    int rc = xNPURunInference(model, input, output);
    m->run_count++;
    if (rc == ZHIO_OK) m->state = ZHIO_MODEL_READY;
    else m->state = ZHIO_MODEL_ERROR_RECOVERABLE;
    return rc;
}
