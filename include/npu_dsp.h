/*
 * npu_dsp.h - NPU/DSP 硬件抽象层
 *
 * 对应《08-NPU-DSP硬件抽象层设计文档》《14-硬件抽象层-HAL接口规范》。
 * 统一设备模型 NPUDevice_t，屏蔽 CMSIS-NN / Ethos-U / eIQ Neutron 差异。
 */
#ifndef ZHIO_NPU_DSP_H
#define ZHIO_NPU_DSP_H

#include <stdint.h>
#include "zhios_types.h"
#include "zhios_err.h"
#include "zhios_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 能力位 */
#define ZHIO_NPU_CAP_NONE      0x0000u
#define ZHIO_NPU_CAP_CMSIS_NN  0x0001u
#define ZHIO_NPU_CAP_ETHOS_U   0x0002u
#define ZHIO_NPU_CAP_NEUTRON   0x0004u

typedef struct NPUDevice NPUDevice_t;
struct NPUDevice {
    const char *name;
    uint32_t    capabilities;
    uint32_t    memory_size;
    int  (*init)(void);
    int  (*load_model)(ModelHandle_t model);
    int  (*run_inference)(ModelHandle_t model, TensorHandle_t in, TensorHandle_t out);
    float (*get_utilization)(void);
    void *priv_data;
};

/* 注册/反注册设备 */
int  iRegisterNPUDevice(NPUDevice_t *dev);
void vNPUUnregisterAll(void);

/* 初始化所有已注册设备，返回成功设备数 */
int  iNPUInit(void);

/* 选择/查询设备 */
NPUDevice_t *xNPUGetDevice(int index);
int          iNPUDeviceCount(void);
NPUDevice_t *xNPUGetDefault(void);   /* 按配置默认后端选择 */

/* 执行推理（选默认设备） */
int xNPURunInference(ModelHandle_t model, TensorHandle_t in, TensorHandle_t out);

/* 内置设备：主机仿真后端（无需 TFLM，用于演示/测试） */
NPUDevice_t *xNPUGetHostSimDevice(void);

#ifdef __cplusplus
}
#endif

#endif /* ZHIO_NPU_DSP_H */
