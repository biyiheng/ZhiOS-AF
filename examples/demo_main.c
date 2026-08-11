/*
 * demo_main.c - ZhiOS-AF 主机演示程序
 *
 * 演示：系统初始化 → 创建 Auto 主 Agent + 子 Agent 团队 →
 *       关键词配置能力偏好 → 推理 → 混合 AI 路由 → config 命令。
 * 对应《32-用户自定义Agent配置指南》与示例应用。
 */
#include <stdio.h>
#include <string.h>
#include "zhios.h"
#include "agent.h"
#include "sub_agents.h"
#include "capability.h"
#include "message_bus.h"
#include "ai_service.h"
#include "comm.h"
#include "cloud_transport.h"
#include "model_runtime.h"
#include "tensor_mem.h"
#include "npu_dsp.h"
#include "inference_scheduler.h"
#include "cloud_adapters.h"

static int mock_transport(const char *url, const char *body, uint32_t body_len,
                          char *resp, uint32_t *resp_len, uint32_t timeout_ms)
{
    (void)url; (void)body; (void)body_len; (void)timeout_ms;
    if (resp && resp_len) { resp[0]='O'; resp[1]='K'; *resp_len=2; }
    return ZHIO_OK;
}

int main(void)
{
    zhio_system_init();
    zhio_cloud_transport_set(mock_transport);   /* 演示用 mock 云端 */

    /* 1) 创建 Auto 主 Agent 与子 Agent 团队 */
    AgentHandle_t auto_agent = xCreateAutoAgent("auto", 2048, 6);
    if (!auto_agent) { printf("create auto agent failed\n"); return 1; }
    iSubAgentsCreateTeam(auto_agent, 1024, 4);

    /* 2) 关键词配置能力偏好 */
    const char *kws[] = {"谨慎模式", "视觉优先", "允许云端"};
    iConfigureAutoAgentByKeywords(auto_agent, kws, 3, 0);
    CapabilityPreference_t p;
    iAgentGetCapability(auto_agent, &p);
    printf("[demo] safety=%d vision=%.2f cloud_allowed=%d\n",
           (int)p.safety_level, p.vision_weight, p.cloud_allowed);

    /* 3) 注册并加载一个本地模型，执行推理 */
    static const uint8_t w[16] = {2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2};
    ModelHandle_t m = xRegisterModel("kws", ZHIO_MODEL_FMT_HOST_SIM, w, sizeof(w), 1);
    iLoadModel(m);
    TensorDesc_t d; memset(&d,0,sizeof(d));
    d.dims=1; d.shape[0]=4; d.type=ZHIO_TENSOR_UINT8; d.size_bytes=4;
    TensorHandle_t in = xAllocTensor(&d), out = xAllocTensor(&d);
    memset(vTensorGetData(in), 0x04, 4);
    int rc = xRunInference(m, in, out, 50);
    printf("[demo] inference rc=%d out0=%u\n", rc,
           (unsigned)((uint8_t*)vTensorGetData(out))[0]);

    /* 4) 混合 AI 路由（CLOUD_ONLY，经 mock 传输） */
    CloudAdapterHandle_t cloud = zhio_openai_adapter_new();
    xRegisterCloudAdapter(cloud, "openai");
    iCloudAdapterInit(cloud, NULL);
    rc = xRouteInference(m, cloud, in, out, (uint32_t)ZHIO_ROUTE_CLOUD_ONLY, 100);
    printf("[demo] route(cloud_only) rc=%d\n", rc);

    /* 5) config 命令（串口/控制台） */
    char resp[64];
    rc = comm_config_parse("config agent \"本地优先+仅本地\"", auto_agent, resp, sizeof(resp));
    printf("[demo] config rc=%d -> %s\n", rc, resp);

    /* 6) 步进驱动 Auto Agent 一轮 */
    iAgentRunStep(auto_agent);
    printf("[demo] agent decisions=%u\n", (unsigned)ulAgentDecisionCount(auto_agent));

    printf("[demo] done\n");
    return 0;
}
