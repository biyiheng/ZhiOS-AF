/*
 * comm.c - 通信协议层（帧协议 + config agent 命令）
 *
 * 对应《18-通信协议规范文档》。
 * 统一帧格式 zhios_frame_t：SOF/LEN/TYPE/PAYLOAD/CRC32/EOF。
 * config agent "关键词+..." 命令解析并调用能力偏好引擎。
 */
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include "comm.h"
#include "capability.h"
#include "zhios_rtos.h"

/* CRC32（IEEE 802.3 反射表） */
uint32_t comm_crc32(const void *data, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFu;
    uint32_t i;
    for (i = 0; i < len; i++) {
        crc ^= p[i];
        uint32_t b;
        for (b = 0; b < 8; b++) {
            uint32_t mask = (uint32_t)-(int32_t)(crc & 1);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

static void put_le16(uint8_t *o, uint16_t v) { o[0] = v & 0xFF; o[1] = (v >> 8) & 0xFF; }
static void put_le32(uint8_t *o, uint32_t v) { o[0]=v&0xFF; o[1]=(v>>8)&0xFF; o[2]=(v>>16)&0xFF; o[3]=(v>>24)&0xFF; }
static uint16_t get_le16(const uint8_t *o) { return (uint16_t)(o[0] | (o[1] << 8)); }

int comm_frame_encode(uint8_t type, const void *payload, uint16_t len,
                      uint8_t *out, uint16_t *out_len)
{
    if (!out || !out_len) return ZHIO_E_INVAL;
    if (len > ZHIO_FRAME_MAX_PAYLOAD) return ZHIO_E_INVAL;
    if (!payload && len > 0) return ZHIO_E_INVAL;

    uint16_t idx = 0;
    out[idx++] = ZHIO_FRAME_SOF;
    put_le16(&out[idx], len); idx += 2;
    out[idx++] = type;
    if (payload && len) { memcpy(&out[idx], payload, len); idx += len; }

    /* CRC 覆盖 LEN+TYPE+载荷 */
    uint32_t crc = comm_crc32(&out[1], (uint32_t)(idx - 1));
    put_le32(&out[idx], crc); idx += 4;
    out[idx++] = ZHIO_FRAME_EOF;

    *out_len = idx;
    return ZHIO_OK;
}

int comm_frame_decode(const uint8_t *buf, uint16_t buf_len, ZhiosFrame_t *frame)
{
    if (!buf || !frame || buf_len < 8) return ZHIO_E_INVAL;
    if (buf[0] != ZHIO_FRAME_SOF) return ZHIO_E_INVAL;
    uint16_t len = get_le16(&buf[1]);
    if (len > ZHIO_FRAME_MAX_PAYLOAD) return ZHIO_E_INVAL;
    uint16_t total = (uint16_t)(1 + 2 + 1 + len + 4 + 1);
    if (buf_len < total) return ZHIO_E_INVAL;
    if (buf[total - 1] != ZHIO_FRAME_EOF) return ZHIO_E_INVAL;

    frame->sof = buf[0];
    frame->len = len;
    frame->type = buf[3];
    if (len) memcpy(frame->payload, &buf[4], len);
    /* CRC 校验 */
    uint32_t expected;
    {
        const uint8_t *c = &buf[4 + len];
        expected = (uint32_t)c[0] | ((uint32_t)c[1] << 8) | ((uint32_t)c[2] << 16) | ((uint32_t)c[3] << 24);
    }
    uint32_t calc = comm_crc32(&buf[1], (uint32_t)(2 + 1 + len)); /* LEN(2B)+TYPE(1B)+载荷(len) */
    if (calc != expected) return ZHIO_E_UNKNOWN;   /* CRC 不匹配 */
    frame->crc32 = expected;
    frame->eof = buf[total - 1];
    return ZHIO_OK;
}

/* config agent "kw1+kw2+..." 解析 */
int comm_config_parse(const char *cmd, AgentHandle_t auto_agent,
                      char *response, uint32_t resp_len)
{
    if (!cmd || !response || resp_len == 0) return ZHIO_E_INVAL;
    if (!auto_agent) {
        snprintf(response, resp_len, "ERR: no agent");
        return ZHIO_E_NOAGENT;
    }
    const char *prefix = "config agent ";
    if (strncmp(cmd, prefix, strlen(prefix)) != 0) {
        snprintf(response, resp_len, "ERR: bad command");
        return ZHIO_E_INVAL;
    }
    const char *p = cmd + strlen(prefix);
    /* 定位双引号 */
    const char *q1 = strchr(p, '"');
    if (!q1) {
        snprintf(response, resp_len, "ERR: missing quotes");
        return ZHIO_E_INVAL;
    }
    const char *q2 = strchr(q1 + 1, '"');
    if (!q2) {
        snprintf(response, resp_len, "ERR: missing closing quote");
        return ZHIO_E_INVAL;
    }

    /* 拆解关键词：'+' 分隔 */
    const char *kws[ZHIO_CFG_MAX_KEYWORDS];
    uint32_t n = 0;
    const char *start = q1 + 1;
    for (; start < q2 && n < ZHIO_CFG_MAX_KEYWORDS; ) {
        const char *sep = strchr(start, '+');
        const char *end = (sep && sep < q2) ? sep : q2;
        uint32_t klen = (uint32_t)(end - start);
        if (klen > 0) {
            char *kw = (char *)zhio_malloc(klen + 1);
            if (!kw) { snprintf(response, resp_len, "ERR: nomem"); return ZHIO_E_NOMEM; }
            memcpy(kw, start, klen);
            kw[klen] = '\0';
            kws[n++] = kw;
        }
        if (!sep || sep >= q2) break;
        start = sep + 1;
    }

    int rc = iConfigureAutoAgentByKeywords(auto_agent, kws, n, 0);
    uint32_t j;
    for (j = 0; j < n; j++) zhio_free((void *)kws[j]);

    if (rc == ZHIO_OK)
        snprintf(response, resp_len, "OK: preference updated");
    else
        snprintf(response, resp_len, "ERR: config failed (%d)", rc);
    return rc;
}
