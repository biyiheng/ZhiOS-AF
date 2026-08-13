/*
 * agent.c - Agent 自治框架实现
 *
 * 对应《09-Agent自治框架设计文档》。
 * 1 个 Auto 主 Agent + N 个子 Agent，运行于内核层；
 * 通过消息总线（inbox 队列）零拷贝通信；看门狗监控。
 * 注：Agent 采用"步进驱动"（iAgentRunStep），便于单元测试与确定性验证；
 *     在真实 MCU 上可由调度器/任务循环周期调用。
 */
#include <string.h>
#include <stdint.h>
#include "agent.h"
#include "message_bus.h"
#include "capability.h"
#include "zhios_config.h"
#include "zhios_rtos.h"

#define ZHIO_MAX_AGENT_SLOTS (ZHIO_CFG_MAX_AGENTS + 1)   /* 8 子 + 1 主 */

typedef struct AutoAgent {
    char                name[16];
    uint32_t            id;
    AgentState_t        state;
    CapabilityPreference_t pref;
    ZhiosQueueHandle_t  inbox;
    AgentBehavior_t     behavior;
    SubAgentType_t      type;
    uint32_t            priority;
    uint32_t            stack_size;
    uint32_t            sub_agents[ZHIO_CFG_MAX_AGENTS];
    uint32_t            sub_count;
    ZhiosTick_t         wd_period;
    ZhiosTick_t         wd_last_feed;
    uint32_t            valid;
    uint32_t            decision_count;
} AutoAgent_t;

static AutoAgent_t g_agents[ZHIO_MAX_AGENT_SLOTS];

static AutoAgent_t *resolve(AgentHandle_t h)
{
    AutoAgent_t *a = (AutoAgent_t *)h;
    return (a && a->valid) ? a : NULL;
}

/* 申请一个 Agent 槽 */
static AutoAgent_t *slot_alloc(const char *name, uint32_t priority, uint32_t stack_size)
{
    uint32_t i;
    for (i = 0; i < ZHIO_MAX_AGENT_SLOTS; i++) {
        if (!g_agents[i].valid) {
            memset(&g_agents[i], 0, sizeof(g_agents[i]));
            strncpy(g_agents[i].name, name, sizeof(g_agents[i].name) - 1);
            g_agents[i].id = i;
            g_agents[i].priority = priority;
            g_agents[i].stack_size = stack_size;
            g_agents[i].state = ZHIO_AGENT_READY;
            g_agents[i].pref = *xCapabilityDefaults();
            g_agents[i].inbox = zhio_queue_create(ZHIO_CFG_AGENT_QUEUE_DEPTH, sizeof(AgentMessage_t));
            if (!g_agents[i].inbox) return NULL;
            g_agents[i].valid = 1;
            return &g_agents[i];
        }
    }
    return NULL;
}

AgentHandle_t xCreateAutoAgent(const char *name, uint32_t stack_size, uint32_t priority)
{
    AutoAgent_t *a = slot_alloc(name, priority, stack_size);
    if (!a) return NULL;
    a->type = (SubAgentType_t)0; /* 主 Agent */
    return (AgentHandle_t)a;
}

AgentHandle_t xCreateSubAgent(AgentHandle_t parent, const char *name,
                              uint32_t stack_size, uint32_t priority)
{
    AutoAgent_t *p = resolve(parent);
    AutoAgent_t *a = slot_alloc(name, priority, stack_size);
    if (!a) return NULL;
    if (p) iAgentRegisterSub(parent, (AgentHandle_t)a);
    return (AgentHandle_t)a;
}

int iAgentRegisterSub(AgentHandle_t parent, AgentHandle_t sub)
{
    AutoAgent_t *p = resolve(parent);
    AutoAgent_t *s = resolve(sub);
    if (!p || !s || p->sub_count >= ZHIO_CFG_MAX_AGENTS) return ZHIO_E_INVAL;
    uint32_t i;
    for (i = 0; i < p->sub_count; i++)
        if (p->sub_agents[i] == s->id) return ZHIO_OK; /* 已注册 */
    p->sub_agents[p->sub_count++] = s->id;
    return ZHIO_OK;
}

void vDeleteAgent(AgentHandle_t agent)
{
    AutoAgent_t *a = resolve(agent);
    if (!a) return;
    if (a->inbox) zhio_queue_reset(a->inbox);
    memset(a, 0, sizeof(*a));
}

AgentState_t eAgentGetState(AgentHandle_t agent)
{
    AutoAgent_t *a = resolve(agent);
    return a ? a->state : ZHIO_AGENT_ERROR;
}

int iAgentSetCapability(AgentHandle_t agent, const CapabilityPreference_t *pref)
{
    AutoAgent_t *a = resolve(agent);
    if (!a || !pref) return ZHIO_E_INVAL;
    a->pref = *pref;
    return ZHIO_OK;
}

int iAgentGetCapability(AgentHandle_t agent, CapabilityPreference_t *pref)
{
    AutoAgent_t *a = resolve(agent);
    if (!a || !pref) return ZHIO_E_INVAL;
    *pref = a->pref;
    return ZHIO_OK;
}

/* 偏好位掩码快捷设置 */
#define ZHIO_PREF_LOCAL_ONLY     (1u << 0)
#define ZHIO_PREF_CLOUD_ALLOWED  (1u << 1)
#define ZHIO_PREF_ACCURACY       (1u << 2)
#define ZHIO_PREF_LATENCY        (1u << 3)

void iSetAgentPreference(AgentHandle_t agent, uint32_t pref_mask)
{
    AutoAgent_t *a = resolve(agent);
    if (!a) return;
    if (pref_mask & ZHIO_PREF_LOCAL_ONLY) {
        a->pref.routing_mode = ZHIO_ROUTE_LOCAL_ONLY;
        a->pref.cloud_allowed = 0;
    }
    if (pref_mask & ZHIO_PREF_CLOUD_ALLOWED) a->pref.cloud_allowed = 1;
    if (pref_mask & ZHIO_PREF_ACCURACY)  { a->pref.accuracy_priority = 1; a->pref.latency_priority = 0; }
    if (pref_mask & ZHIO_PREF_LATENCY)   { a->pref.latency_priority = 1; a->pref.accuracy_priority = 0; }
}

/* 消息发送（包装消息总线，自动填 src 与类型） */
int xSendAgentMessage(AgentHandle_t dst, const void *msg, uint32_t msg_len, ZhiosTick_t timeout)
{
    AutoAgent_t *d = resolve(dst);
    if (!d) return ZHIO_E_NOAGENT;
    AgentMessage_t m;
    memset(&m, 0, sizeof(m));
    m.id = 0; m.dst = d->id; m.type = ZHIO_MSG_REQUEST;
    m.priority = 5; m.deadline = timeout;
    m.data = msg; m.data_len = msg_len;
    return iMessageBusSend(d->inbox, &m, timeout);
}

int xReceiveAgentMessage(AgentHandle_t self, void *buf, uint32_t *buf_len, ZhiosTick_t timeout)
{
    AutoAgent_t *s = resolve(self);
    if (!s) return ZHIO_E_NOAGENT;
    AgentMessage_t m;
    return iMessageBusReceive(s->inbox, &m, buf, buf_len, timeout);
}

/* 看门狗 */
int iAgentWatchdogStart(AgentHandle_t agent, ZhiosTick_t period)
{
    AutoAgent_t *a = resolve(agent);
    if (!a) return ZHIO_E_INVAL;
    a->wd_period = period;
    a->wd_last_feed = zhio_get_tick();
    return ZHIO_OK;
}
void xAgentWatchdogFeed(AgentHandle_t agent)
{
    AutoAgent_t *a = resolve(agent);
    if (a) a->wd_last_feed = zhio_get_tick();
}
int zhio_agent_watchdog_expired(AgentHandle_t agent)
{
    AutoAgent_t *a = resolve(agent);
    if (!a || a->wd_period == 0) return 0;
    return (zhio_get_tick() - a->wd_last_feed > a->wd_period) ? 1 : 0;
}

int iAgentSetBehavior(AgentHandle_t agent, const AgentBehavior_t *behavior, SubAgentType_t type)
{
    AutoAgent_t *a = resolve(agent);
    if (!a || !behavior) return ZHIO_E_INVAL;
    a->behavior = *behavior;
    a->type = type;
    return ZHIO_OK;
}

/* 步进执行：驱动 Agent 行为一轮，喂狗，维护状态机 */
int iAgentRunStep(AgentHandle_t agent)
{
    AutoAgent_t *a = resolve(agent);
    if (!a) return ZHIO_E_NOAGENT;
    if (a->state == ZHIO_AGENT_ERROR) return ZHIO_E_UNKNOWN;
    a->state = ZHIO_AGENT_EXECUTING;
    if (a->behavior.run) a->behavior.run(agent);
    a->state = ZHIO_AGENT_READY;
    a->decision_count++;
    xAgentWatchdogFeed(agent);
    return ZHIO_OK;
}

uint32_t ulAgentDecisionCount(AgentHandle_t agent)
{
    AutoAgent_t *a = resolve(agent);
    return a ? a->decision_count : 0;
}

/* 监督巡检：检测并自愈"卡死"（看门狗超时）的 Agent */
int iAgentSupervise(void)
{
    uint32_t i, recovered = 0;
    ZhiosTick_t now = zhio_get_tick();
    for (i = 0; i < ZHIO_MAX_AGENT_SLOTS; i++) {
        AutoAgent_t *a = &g_agents[i];
        if (!a->valid || a->wd_period == 0) continue;
        /* 看门狗超时判定（回绕安全） */
        if ((now - a->wd_last_feed) > a->wd_period) {
            zhio_log("[agent] SUPERVISE: agent[%u]='%s' STALLED (wd=%u ms since feed=%u) -> recovering",
                     (unsigned)i, a->name, (unsigned)a->wd_period,
                     (unsigned)(now - a->wd_last_feed));
            /* 自愈：复位状态与看门狗，避免单个卡死进程阻塞调度 */
            a->state = ZHIO_AGENT_READY;
            a->wd_last_feed = now;
            recovered++;
        }
    }
    if (recovered) zhio_log("[agent] SUPERVISE: recovered %u stalled agent(s)", (unsigned)recovered);
    return (int)recovered;
}
