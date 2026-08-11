/*
 * test_agent.c - Agent/能力/消息总线/混合AI路由 单元测试
 *
 * 运行方式：由 test_main 调用 ztest_run_agent()。
 */
#include <string.h>
#include "ztest.h"
#include "zhios.h"
#include "agent.h"
#include "sub_agents.h"
#include "message_bus.h"
#include "capability.h"
#include "ai_service.h"
#include "comm.h"
#include "cloud_transport.h"
#include "cloud_adapters.h"
#include "model_runtime.h"
#include "tensor_mem.h"
#include "npu_dsp.h"
#include "inference_scheduler.h"

/* ---------- 能力偏好引擎（关键词配置） ---------- */
static void test_capability_keywords(void)
{
    iCapabilityInit();
    AgentHandle_t a = xCreateAutoAgent("auto", 1024, 5);
    ZTEST_ASSERT_NOT_NULL(a);
    const char *kws[] = {"谨慎模式", "视觉优先", "允许云端"};
    ZTEST_ASSERT_EQ(iConfigureAutoAgentByKeywords(a, kws, 3, 0), ZHIO_OK);
    CapabilityPreference_t p; memset(&p,0,sizeof(p));
    ZTEST_ASSERT_EQ(iAgentGetCapability(a, &p), ZHIO_OK);
    ZTEST_ASSERT_EQ((int)p.safety_level, (int)ZHIO_SAFETY_HIGH);
    ZTEST_ASSERT((p.vision_weight > 0.79f) && (p.vision_weight <= 0.81f));
    ZTEST_ASSERT_EQ(p.cloud_allowed, 1);
    /* 默认本地优先 */
    ZTEST_ASSERT_EQ((int)p.routing_mode, (int)ZHIO_ROUTE_LOCAL_FIRST);
}

static void test_capability_safety_level(void)
{
    iCapabilityInit();
    AgentHandle_t a = xCreateAutoAgent("auto2", 1024, 5);
    ZTEST_ASSERT_NOT_NULL(a);
    ZTEST_ASSERT_EQ(iSetAgentSafetyLevel(a, ZHIO_SAFETY_CRITICAL), ZHIO_OK);
    CapabilityPreference_t p; memset(&p,0,sizeof(p));
    iAgentGetCapability(a, &p);
    ZTEST_ASSERT_EQ((int)p.safety_level, (int)ZHIO_SAFETY_CRITICAL);
    ZTEST_ASSERT_EQ(p.cloud_allowed, 0);                 /* 最高安全禁用云端 */
    ZTEST_ASSERT_EQ((int)p.routing_mode, (int)ZHIO_ROUTE_LOCAL_ONLY);
}

/* ---------- 消息总线 ---------- */
static void test_message_bus(void)
{
    iMessageBusInit();
    AgentHandle_t a = xCreateAutoAgent("src", 1024, 5);
    AgentHandle_t b = xCreateAutoAgent("dst", 1024, 5);
    const char payload[] = "hello-agent";
    ZTEST_ASSERT_EQ(xSendAgentMessage(b, payload, strlen(payload), 100), ZHIO_OK);
    char buf[64]; uint32_t blen = sizeof(buf);
    ZTEST_ASSERT_EQ(xReceiveAgentMessage(b, buf, &blen, 100), ZHIO_OK);
    ZTEST_ASSERT_EQ(blen, (uint32_t)strlen(payload));
    ZTEST_ASSERT_EQ(memcmp(buf, payload, blen), 0);
}

/* ---------- 子 Agent 团队 + 步进 ---------- */
static void test_sub_agent_team(void)
{
    iMessageBusInit();
    iCapabilityInit();
    AgentHandle_t auto_a = xCreateAutoAgent("auto", 2048, 6);
    ZTEST_ASSERT_NOT_NULL(auto_a);
    ZTEST_ASSERT_EQ(iSubAgentsCreateTeam(auto_a, 1024, 4), ZHIO_OK);
    ZTEST_ASSERT_EQ(iAgentRunStep(auto_a), ZHIO_OK);
    ZTEST_ASSERT_EQ(ulAgentDecisionCount(auto_a), 1u);
}

/* ---------- config agent 命令 ---------- */
static void test_config_command(void)
{
    iCapabilityInit();
    iMessageBusInit();
    AgentHandle_t a = xCreateAutoAgent("auto", 1024, 5);
    char resp[64];
    int rc = comm_config_parse("config agent \"本地优先+仅本地\"", a, resp, sizeof(resp));
    ZTEST_ASSERT_EQ(rc, ZHIO_OK);
    ZTEST_ASSERT_EQ(strncmp(resp, "OK:", 3), 0);
    CapabilityPreference_t p; memset(&p,0,sizeof(p));
    iAgentGetCapability(a, &p);
    ZTEST_ASSERT_EQ((int)p.routing_mode, (int)ZHIO_ROUTE_LOCAL_ONLY);
    ZTEST_ASSERT_EQ(p.cloud_allowed, 0);
    /* 错误命令 */
    ZTEST_ASSERT(comm_config_parse("foo bar", a, resp, sizeof(resp)) != ZHIO_OK);
}

/* ---------- 混合 AI 路由（mock 传输） ---------- */
static int mock_transport(const char *url, const char *body, uint32_t body_len,
                          char *resp, uint32_t *resp_len, uint32_t timeout_ms)
{
    (void)url; (void)body; (void)body_len; (void)timeout_ms;
    if (resp && resp_len) { resp[0]='O'; resp[1]='K'; *resp_len = 2; }
    return ZHIO_OK;
}

static void test_ai_route_cloud_only(void)
{
    iTensorPoolInit(0);
    iModelRuntimeInit();
    iAIServiceInit();
    iSecurityInit();
    vNPUUnregisterAll(); iRegisterNPUDevice(xNPUGetHostSimDevice()); iNPUInit();
    zhio_cloud_transport_set(mock_transport);

    static const uint8_t w[8]={1,1,1,1,1,1,1,1};
    ModelHandle_t m = xRegisterModel("m", ZHIO_MODEL_FMT_HOST_SIM, w, sizeof(w), 1);
    iLoadModel(m);

    /* 验证 mock 传输 + 适配器工厂可用 */
    CloudAdapterHandle_t oa = zhio_openai_adapter_new();
    ZTEST_ASSERT_NOT_NULL(oa);
    ZTEST_ASSERT_EQ(xRegisterCloudAdapter(oa, "openai"), ZHIO_OK);
    ZTEST_ASSERT_EQ(iCloudAdapterInit(oa, NULL), ZHIO_OK);

    /* 经 openai 适配器 + mock 传输做 chat completion */
    const char *prompt = "hello";
    char rsp[64];
    uint32_t rlen = sizeof(rsp);
    ZTEST_ASSERT_EQ(iCloudChatCompletion(oa, prompt, rsp, &rlen, 100), ZHIO_OK);
    ZTEST_ASSERT_EQ(rlen, 2u);
    ZTEST_ASSERT_EQ(memcmp(rsp, "OK", 2), 0);

    /* 路由：CLOUD_ONLY 应调用云端并成功 */
    TensorDesc_t d; memset(&d,0,sizeof(d)); d.dims=1; d.shape[0]=4; d.type=ZHIO_TENSOR_UINT8; d.size_bytes=4;
    TensorHandle_t ti = xAllocTensor(&d), to = xAllocTensor(&d);
    memset(vTensorGetData(ti), 1, 4);
    uint32_t flags = (uint32_t)ZHIO_ROUTE_CLOUD_ONLY;
    ZTEST_ASSERT_EQ(xRouteInference(m, oa, ti, to, flags, 100), ZHIO_OK);
}

void ztest_run_agent(void)
{
    ZTEST_RUN(test_capability_keywords);
    ZTEST_RUN(test_capability_safety_level);
    ZTEST_RUN(test_message_bus);
    ZTEST_RUN(test_sub_agent_team);
    ZTEST_RUN(test_config_command);
    ZTEST_RUN(test_ai_route_cloud_only);
}
