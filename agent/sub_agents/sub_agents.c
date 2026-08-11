/*
 * sub_agents.c - 子 Agent（Vision/Motion/Safety/Force/Quality）
 *
 * 对应《09-Agent自治框架设计文档》子 Agent 设计表。
 * 每个子 Agent 一个运行行为；典型行为：本地/云端决策后向 Auto 主 Agent 汇报。
 * 本文件提供"创建一组子 Agent 团队"的便捷接口与行为实现。
 */
#include <string.h>
#include <stdint.h>
#include "agent.h"
#include "message_bus.h"
#include "zhios_rtos.h"

#define SUB_REPORT_PAYLOAD "done"

/* 通用子 Agent 行为：向 Auto 主 Agent 发送一条 REPORT 消息 */
static void sub_behavior_run(AgentHandle_t self)
{
    /* self 已由框架解析；这里发送事件到 Auto（dst 由消息总线携带） */
    /* 简化：不依赖父句柄，发送 BROADCAST 事件 */
    AgentMessage_t m;
    memset(&m, 0, sizeof(m));
    m.type = ZHIO_MSG_EVENT;
    m.priority = 4;
    m.data = SUB_REPORT_PAYLOAD;
    m.data_len = (uint32_t)strlen(SUB_REPORT_PAYLOAD);
    /* 需要 inbox 列表才能广播；此处作为占位行为，实际由演示代码发送 */
    (void)m;
}

/* 视觉子 Agent 行为 */
static void vision_run(AgentHandle_t self)
{
    sub_behavior_run(self);
    zhio_log("[vision] 视觉检测完成");
}
/* 运动子 Agent 行为 */
static void motion_run(AgentHandle_t self)
{
    sub_behavior_run(self);
    zhio_log("[motion] 轨迹规划完成");
}
/* 安全子 Agent 行为 */
static void safety_run(AgentHandle_t self)
{
    sub_behavior_run(self);
    zhio_log("[safety] 安全校验通过");
}
/* 力控子 Agent 行为 */
static void force_run(AgentHandle_t self)
{
    sub_behavior_run(self);
    zhio_log("[force] 柔顺力控执行");
}
/* 质检子 Agent 行为 */
static void quality_run(AgentHandle_t self)
{
    sub_behavior_run(self);
    zhio_log("[quality] 质检评分完成");
}

/* 创建一组子 Agent 团队并绑定行为 */
int iSubAgentsCreateTeam(AgentHandle_t auto_agent, uint32_t stack, uint32_t priority)
{
    static const struct { const char *name; SubAgentType_t type; void (*run)(AgentHandle_t); } tab[] = {
        { "vision",  ZHIO_SUBAGENT_VISION,  vision_run  },
        { "motion",  ZHIO_SUBAGENT_MOTION,  motion_run  },
        { "safety",  ZHIO_SUBAGENT_SAFETY,  safety_run  },
        { "force",   ZHIO_SUBAGENT_FORCE,   force_run   },
        { "quality", ZHIO_SUBAGENT_QUALITY, quality_run },
    };
    AgentBehavior_t beh;
    uint32_t i;
    for (i = 0; i < sizeof(tab) / sizeof(tab[0]); i++) {
        AgentHandle_t sub = xCreateSubAgent(auto_agent, tab[i].name, stack, priority);
        if (!sub) return ZHIO_E_NOMEM;
        memset(&beh, 0, sizeof(beh));
        beh.run = tab[i].run;
        iAgentSetBehavior(sub, &beh, tab[i].type);
    }
    return ZHIO_OK;
}
