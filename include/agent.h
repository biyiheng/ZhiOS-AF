/*
 * agent.h - Agent 自治框架
 *
 * 对应《09-Agent自治框架设计文档》《17-API接口规范文档》第 6 节。
 * 1 个 Auto 主 Agent + N 个子 Agent（Vision/Motion/Safety/Force/Quality），
 * 全部运行于内核层。Agent 通过消息总线零拷贝通信。
 */
#ifndef ZHIO_AGENT_H
#define ZHIO_AGENT_H

#include <stdint.h>
#include "zhios_types.h"
#include "zhios_err.h"
#include "zhios_config.h"
#include "zhios_rtos.h"
#include "capability.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 子 Agent 类型枚举 */
typedef enum {
    ZHIO_SUBAGENT_VISION  = 0,   /* 视觉检测/位姿估计 */
    ZHIO_SUBAGENT_MOTION,        /* 运动学/轨迹规划 */
    ZHIO_SUBAGENT_SAFETY,        /* 安全监控/碰撞检测 */
    ZHIO_SUBAGENT_FORCE,         /* 柔顺力控/轴孔装配 */
    ZHIO_SUBAGENT_QUALITY,       /* 缺陷检测/质量评分 */
} SubAgentType_t;

typedef void (*AgentTaskFn_t)(AgentHandle_t self);

/* Agent 任务执行上下文（主循环每轮调用） */
typedef struct {
    AgentTaskFn_t run;
} AgentBehavior_t;

/* 创建 Auto 主 Agent / 子 Agent */
AgentHandle_t xCreateAutoAgent(const char *name, uint32_t stack_size, uint32_t priority);
AgentHandle_t xCreateSubAgent(AgentHandle_t parent, const char *name,
                              uint32_t stack_size, uint32_t priority);

/* 配置（关键词 / 偏好 / 安全等级），forward 到 capability 引擎 */
int  iConfigureAutoAgentByKeywords(AgentHandle_t agent, const char **keywords,
                                   uint32_t keyword_count, uint32_t flags);
void iSetAgentPreference(AgentHandle_t agent, uint32_t pref_mask);
int  iSetAgentSafetyLevel(AgentHandle_t agent, uint32_t level);

/* 消息（公开 API，包装消息总线） */
int  xSendAgentMessage(AgentHandle_t dst, const void *msg, uint32_t msg_len, ZhiosTick_t timeout);
int  xReceiveAgentMessage(AgentHandle_t self, void *buf, uint32_t *buf_len, ZhiosTick_t timeout);

/* 看门狗 */
int  iAgentWatchdogStart(AgentHandle_t agent, ZhiosTick_t period);
void xAgentWatchdogFeed(AgentHandle_t agent);

/* 生命周期/查询 */
void          vDeleteAgent(AgentHandle_t agent);
AgentState_t  eAgentGetState(AgentHandle_t agent);
int           iAgentRegisterSub(AgentHandle_t parent, AgentHandle_t sub);

/* 为子 Agent 绑定行为（内部/高级使用） */
int  iAgentSetBehavior(AgentHandle_t agent, const AgentBehavior_t *behavior, SubAgentType_t type);

/* 步进执行（供测试/演示/调度器驱动）与决策统计 */
int      iAgentRunStep(AgentHandle_t agent);
uint32_t ulAgentDecisionCount(AgentHandle_t agent);

/* 监督巡检（AI 辅助决策，进程卡死自愈）
 * 遍历全部 Agent：若某 Agent 看门狗超时（视为卡死/失去响应），则将其状态
 * 重置回 ZHIO_AGENT_READY、清零其看门狗计时并输出恢复日志，使系统可继续
 * 调度而不被单个卡死进程阻塞。返回本次恢复（被重置）的 Agent 数量。
 * 应由主循环/低优先级巡检任务周期调用（周期取各 Agent 看门狗上限的公共值）。
 */
int iAgentSupervise(void);

/* 能力偏好访问（供 capability 引擎使用） */
int iAgentSetCapability(AgentHandle_t agent, const CapabilityPreference_t *pref);
int iAgentGetCapability(AgentHandle_t agent, CapabilityPreference_t *pref);

#ifdef __cplusplus
}
#endif

#endif /* ZHIO_AGENT_H */
