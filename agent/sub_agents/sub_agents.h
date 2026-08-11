/*
 * sub_agents.h - 子 Agent 团队便捷接口
 *
 * 对应《09-Agent自治框架设计文档》。
 */
#ifndef ZHIO_SUB_AGENTS_H
#define ZHIO_SUB_AGENTS_H

#include "zhios_types.h"
#include "zhios_err.h"
#include "agent.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 在 auto_agent 下创建 5 个子 Agent（Vision/Motion/Safety/Force/Quality）并绑定行为 */
int iSubAgentsCreateTeam(AgentHandle_t auto_agent, uint32_t stack, uint32_t priority);

#ifdef __cplusplus
}
#endif

#endif /* ZHIO_SUB_AGENTS_H */
