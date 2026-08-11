/*
 * kernel.c - ZhiOS-AF 系统初始化与启动
 *
 * 统一初始化各层子系统（RTOS、张量内存、模型运行时、NPU 设备、
 * 调度器、安全、消息总线、能力引擎、AI 服务、板级）。
 */
#include <string.h>
#include <stdint.h>
#include "zhios.h"
#include "npu_dsp.h"
#include "tensor_mem.h"
#include "model_runtime.h"
#include "inference_scheduler.h"
#include "security.h"
#include "firewall.h"
#include "message_bus.h"
#include "capability.h"
#include "ai_service.h"
#include "hal.h"

int zhio_system_init(void)
{
    /* 板级（BSP） */
    int rc = board_init();
    if (rc != ZHIO_OK) return rc;

    /* 第 2 层 AI 内核 */
    iInferenceSchedulerInit();
    rc = iTensorPoolInit(0);
    if (rc != ZHIO_OK) return rc;

    /* 第 3 层 模型运行时 */
    iModelRuntimeInit();

    /* NPU 设备（默认注册主机仿真后端） */
    vNPUUnregisterAll();
    iRegisterNPUDevice(xNPUGetHostSimDevice());
    iNPUInit();

    /* 安全 */
    iSecurityInit();

    /* 通信防火墙（默认拒绝一切，显式放行受信任命令） */
    iFirewallInit();
    iFirewallSetMaxPayload(512u);
    /* 受信任的本地/上位机命令白名单（按需求扩展） */
    iFirewallAllowCmd(0x01u);   /* 配置命令 */
    iFirewallAllowCmd(0x02u);   /* 推理触发 */
    iFirewallAllowCmd(0x03u);   /* Agent 控制 */
    iFirewallAllowCmd(0x10u);   /* 遥测上报 */

    /* 第 4.5 层 */
    iCapabilityInit();
    iMessageBusInit();

    /* 第 3.5 层 */
    iAIServiceInit();

    zhio_log("[kernel] ZhiOS-AF v%s initialized", ZHIO_VERSION_STRING);
    return ZHIO_OK;
}

void zhio_system_start(void)
{
    /* host：无调度器需启动；真实 MCU 下调用 FreeRTOS 调度器 */
    zhio_log("[kernel] system start");
}
