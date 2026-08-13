/*
 * npu_dsp.c - NPU/DSP 硬件抽象层实现
 *
 * 对应《08-NPU-DSP硬件抽象层设计文档》。
 * 维护设备注册表，并提供"主机仿真后端"（无需 TFLM，用于演示/单元测试）。
 * 真实后端（CMSIS-NN/Ethos-U/eIQ Neutron）通过 iRegisterNPUDevice 注册即可。
 *
 * =============================================================================
 * 模块说明（维护入口）
 * -----------------------------------------------------------------------------
 * 职责     ：NPU/DSP 设备注册表 + 默认设备选择 + 主机仿真后端（确定性推理）。
 * 依赖     ：tensor_mem（张量数据读/写）、npu_dsp.h、zhios_config、zhios_rtos。
 * 被谁调用 ：model_runtime、inference_scheduler（xRunInference→xNPURunInference）、
 *             kernel.c（初始化注册）、外部 include/npu_dsp.h。
 * 内存     ：通过张量句柄零拷贝读写（vTensorGetData/ulTensorGetSize），不自行申请
 *             热路径内存；对应《33》4.2 内存管理与 4.4 设备驱动维度。
 * 可替换   ：真实加速器仅需实现 NPUDevice_t 回调并经 iRegisterNPUDevice 注册，
 *             上层零改动（见 bsp/PORTING.md）。
 * =============================================================================
 */
#include <string.h>
#include <stdint.h>
#include "npu_dsp.h"
#include "tensor_mem.h"
#include "zhios_config.h"
#include "zhios_rtos.h"

#define ZHIO_MAX_NPU_DEVICES 4

static NPUDevice_t *g_devices[ZHIO_MAX_NPU_DEVICES];
static int g_device_count = 0;

/* ---------------- 主机仿真后端 ---------------- */
static int hostsim_init(void) { return ZHIO_OK; }

static int hostsim_load_model(ModelHandle_t model) { (void)model; return ZHIO_OK; }

static int hostsim_run_inference(ModelHandle_t model, TensorHandle_t in, TensorHandle_t out)
{
    (void)model;
    const uint8_t *idata = (const uint8_t *)vTensorGetData(in);
    uint8_t *odata = (uint8_t *)vTensorGetData(out);
    uint32_t isize = ulTensorGetSize(in);
    uint32_t osize = ulTensorGetSize(out);
    if (!idata || !odata || osize == 0) return ZHIO_E_INVAL;

    /* 仿真推理：基于输入数据计算确定性输出 */
    uint32_t sum = 0, i;
    for (i = 0; i < isize && i < 4096; i++) sum += idata[i];
    /* 输出第一个值 = 输入字节和，其余填充模式 */
    odata[0] = (uint8_t)(sum & 0xFF);
    for (i = 1; i < osize; i++) odata[i] = (uint8_t)(sum + i);
    return ZHIO_OK;
}

static float hostsim_utilization(void) { return 0.0f; }

static NPUDevice_t g_hostsim_dev = {
    .name = "host-sim",
    .capabilities = ZHIO_NPU_CAP_NONE,
    .memory_size = 0,
    .init = hostsim_init,
    .load_model = hostsim_load_model,
    .run_inference = hostsim_run_inference,
    .get_utilization = hostsim_utilization,
    .priv_data = NULL,
};

NPUDevice_t *xNPUGetHostSimDevice(void) { return &g_hostsim_dev; }

/* ---------------- 注册表 ---------------- */
int iRegisterNPUDevice(NPUDevice_t *dev)
{
    if (!dev) return ZHIO_E_INVAL;
    if (g_device_count >= ZHIO_MAX_NPU_DEVICES) return ZHIO_E_NOMEM;
    uint32_t i;
    for (i = 0; i < (uint32_t)g_device_count; i++)
        if (g_devices[i] == dev) return ZHIO_OK; /* 已注册 */
    g_devices[g_device_count++] = dev;
    return ZHIO_OK;
}

void vNPUUnregisterAll(void)
{
    g_device_count = 0;
    memset(g_devices, 0, sizeof(g_devices));
}

int iNPUInit(void)
{
    int ok = 0;
    uint32_t i;
    for (i = 0; i < (uint32_t)g_device_count; i++) {
        if (g_devices[i] && g_devices[i]->init)
            if (g_devices[i]->init() == ZHIO_OK) ok++;
    }
    return ok;
}

int iNPUDeviceCount(void) { return g_device_count; }

NPUDevice_t *xNPUGetDevice(int index)
{
    if (index < 0 || index >= g_device_count) return NULL;
    return g_devices[index];
}

NPUDevice_t *xNPUGetDefault(void)
{
    if (g_device_count > 0) return g_devices[0];
    return &g_hostsim_dev;   /* 兜底：主机仿真 */
}

int xNPURunInference(ModelHandle_t model, TensorHandle_t in, TensorHandle_t out)
{
    NPUDevice_t *dev = xNPUGetDefault();
    if (!dev || !dev->run_inference) return ZHIO_E_NODEVICE;
    return dev->run_inference(model, in, out);
}
