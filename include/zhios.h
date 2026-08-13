/*
 * zhios.h - ZhiOS-AF 总入口头文件
 *
 * 统一包含所有公共接口，供应用层（app/、examples/）使用。
 */
#ifndef ZHIO_H
#define ZHIO_H

#include "zhios_config.h"
#include "zhios_err.h"
#include "zhios_types.h"
#include "zhios_rtos.h"

#include "inference_scheduler.h"
#include "tensor_mem.h"
#include "model_runtime.h"
#include "npu_dsp.h"
#include "security.h"
#include "agent.h"
#include "message_bus.h"
#include "ai_service.h"
#include "capability.h"
#include "hal.h"
#include "comm.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 系统级初始化：初始化 RTOS、张量内存池、NPU 设备、路由等。
 * board_init 由各 BSP 提供（见 hal.h），在 zhio_system_init 之前或之后调用均可，
 * 但张量池初始化需要内存已就绪。
 */
int  zhio_system_init(void);
void zhio_system_start(void);   /* 启动内核（若目标需要则调用调度器；host 下为空） */

#ifdef __cplusplus
}
#endif

#endif /* ZHIO_H */
