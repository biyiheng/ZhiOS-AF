/*
 * zhios_err.h - ZhiOS-AF 通用错误码
 *
 * 对应《17-API接口规范文档》第 3 节。所有 API 返回 int，
 * 非负表示成功（ZHIO_OK），负值为具体错误码。
 */
#ifndef ZHIO_ERR_H
#define ZHIO_ERR_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ZHIO_OK            =  0,   /* 成功 */
    ZHIO_E_NOMEM       = -1,   /* 内存不足 */
    ZHIO_E_INVAL       = -2,   /* 非法参数 */
    ZHIO_E_NOTFOUND    = -3,   /* 未找到资源/句柄 */
    ZHIO_E_BUSY        = -4,   /* 资源忙，稍后重试 */
    ZHIO_E_TIMEOUT     = -5,   /* 操作超时 */
    ZHIO_E_BADMODEL    = -6,   /* 模型无效/加载失败 */
    ZHIO_E_NODEVICE    = -7,   /* 硬件设备不可用 */
    ZHIO_E_NOCLOUD     = -8,   /* 云端服务不可达 */
    ZHIO_E_NOAGENT     = -9,   /* Agent 不存在/未就绪 */
    ZHIO_E_SAFETY      = -10,  /* 安全校验未通过 */
    ZHIO_E_CANCELED    = -11,  /* 操作被取消 */
    ZHIO_E_NOSUPPORT   = -12,  /* 功能不支持 */
    ZHIO_E_UNKNOWN     = -13,  /* 未知错误 */
    ZHIO_E_DENIED      = -14,  /* 被防火墙/安全策略拒绝 */
} zhios_err_t;

#ifdef __cplusplus
}
#endif

#endif /* ZHIO_ERR_H */
