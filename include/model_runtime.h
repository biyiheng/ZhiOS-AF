/*
 * model_runtime.h - 模型运行时（模型即对象）
 *
 * 对应《07-模型运行时设计文档》。
 * 模型状态机：UNLOADED→LOADING→READY→RUNNING→READY→UNLOADED，
 * 以及 ERROR_RECOVERABLE / ERROR_FATAL。
 */
#ifndef ZHIO_MODEL_RUNTIME_H
#define ZHIO_MODEL_RUNTIME_H

#include <stdint.h>
#include "zhios_types.h"
#include "zhios_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 模型元数据 */
typedef struct {
    char        name[24];
    ModelFormat_t format;
    uint32_t    size;
    uint32_t    version;
    ModelState_t state;
    uint32_t    checksum;      /* 当前数据的校验和 */
    uint32_t    run_count;
} ModelInfo_t;

int  iModelRuntimeInit(void);
void vModelRuntimeDeinit(void);

/* 注册模型（data 通常来自 const 区或持久区拷贝；此处拷贝到持久区） */
ModelHandle_t xRegisterModel(const char *name,
                             ModelFormat_t fmt,
                             const void  *data,
                             uint32_t     size,
                             uint32_t     version);

/* 加载/卸载/热切换（<100ms 目标，双缓冲乒乓） */
int  iLoadModel(ModelHandle_t model);
int  iUnloadModel(ModelHandle_t model);
int  iSwapModel(ModelHandle_t model, const void *new_data, uint32_t new_size, uint32_t new_version);
int  iVerifyModelChecksum(ModelHandle_t model);

/* 查询 */
int          iModelGetInfo(ModelHandle_t model, ModelInfo_t *info);
ModelState_t eModelGetState(ModelHandle_t model);
int          iModelSetState(ModelHandle_t model, ModelState_t st); /* 供执行层更新 RUNNING */

/* 执行（后端由 npu_dsp 提供；供推理 API 调用） */
int xModelExecute(ModelHandle_t model, TensorHandle_t input, TensorHandle_t output);

/* 安全校验回调绑定（内部，供 security 使用） */
int iModelBindValidator(ModelHandle_t model, void *validator);
void *zhio_model_get_validator(ModelHandle_t model);

#ifdef __cplusplus
}
#endif

#endif /* ZHIO_MODEL_RUNTIME_H */
