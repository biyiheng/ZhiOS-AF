/*
 * test_core.c - 核心模块单元测试（张量内存/模型运行时/调度器/安全/通信）
 *
 * 运行方式：由 test_main 调用 ztest_run_core()。
 */
#include <string.h>
#include "ztest.h"
#include "zhios.h"
#include "tensor_mem.h"
#include "model_runtime.h"
#include "inference_scheduler.h"
#include "security.h"
#include "comm.h"

/* ---------- 张量内存 ---------- */
static void test_tensor_basic(void)
{
    iTensorPoolInit(0);
    TensorDesc_t d;
    memset(&d, 0, sizeof(d));
    d.dims = 4; d.shape[0]=1; d.shape[1]=4; d.shape[2]=4; d.shape[3]=3;
    d.type = ZHIO_TENSOR_INT8; d.size_bytes = 1*4*4*3;
    TensorHandle_t t = xAllocTensor(&d);
    ZTEST_ASSERT_NOT_NULL(t);
    ZTEST_ASSERT_EQ(iTensorIsValid(t), 1);
    void *p = vTensorGetData(t);
    ZTEST_ASSERT_NOT_NULL(p);
    memset(p, 0x11, d.size_bytes);
    ZTEST_ASSERT_EQ(ulTensorGetSize(t), d.size_bytes);
    vFreeTensor(t);
    ZTEST_ASSERT_EQ(iTensorIsValid(t), 0);
}

static void test_tensor_oom(void)
{
    iTensorPoolInit(0);
    TensorDesc_t d; memset(&d,0,sizeof(d));
    d.dims=1; d.shape[0]=1; d.type=ZHIO_TENSOR_UINT8;
    d.size_bytes = 1024u * 1024u * 1024u;   /* 超大，应分配失败 */
    TensorHandle_t t = xAllocTensor(&d);
    ZTEST_ASSERT_EQ((t == NULL), 1);
}

static void test_tensor_stats(void)
{
    iTensorPoolInit(0);
    TensorPoolStats_t s; memset(&s,0,sizeof(s));
    ZTEST_ASSERT_EQ(xGetTensorPoolStats(&s), ZHIO_OK);
    ZTEST_ASSERT(s.total_bytes > 0);
    ZTEST_ASSERT_EQ(s.free_bytes + s.used_bytes, s.total_bytes);
    ZTEST_ASSERT_EQ(xDefragmentPool(0), ZHIO_OK);
}

/* ---------- 模型运行时 ---------- */
static void test_model_lifecycle(void)
{
    iTensorPoolInit(0);
    iModelRuntimeInit();
    vNPUUnregisterAll();
    iRegisterNPUDevice(xNPUGetHostSimDevice());
    iNPUInit();

    static const uint8_t w[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
    ModelHandle_t m = xRegisterModel("kws", ZHIO_MODEL_FMT_HOST_SIM, w, sizeof(w), 1);
    ZTEST_ASSERT_NOT_NULL(m);
    ZTEST_ASSERT_EQ(eModelGetState(m), ZHIO_MODEL_UNLOADED);
    ZTEST_ASSERT_EQ(iVerifyModelChecksum(m), ZHIO_OK);
    ZTEST_ASSERT_EQ(iLoadModel(m), ZHIO_OK);
    ZTEST_ASSERT_EQ(eModelGetState(m), ZHIO_MODEL_READY);

    /* 执行推理 */
    TensorDesc_t in, out; memset(&in,0,sizeof(in)); memset(&out,0,sizeof(out));
    in.dims=1; in.shape[0]=4; in.type=ZHIO_TENSOR_UINT8; in.size_bytes=4;
    out.dims=1; out.shape[0]=4; out.type=ZHIO_TENSOR_UINT8; out.size_bytes=4;
    TensorHandle_t ti = xAllocTensor(&in);
    TensorHandle_t to = xAllocTensor(&out);
    memset(vTensorGetData(ti), 0x03, 4);
    ZTEST_ASSERT_EQ(xModelExecute(m, ti, to), ZHIO_OK);
    const uint8_t *od = (const uint8_t*)vTensorGetData(to);
    ZTEST_ASSERT_EQ(od[0], 0x0C);   /* 3*4=12 */

    ModelInfo_t info; memset(&info,0,sizeof(info));
    ZTEST_ASSERT_EQ(iModelGetInfo(m, &info), ZHIO_OK);
    ZTEST_ASSERT_EQ(info.version, 1u);
    ZTEST_ASSERT_EQ(info.run_count, 1u);

    /* 热切换 */
    static const uint8_t w2[16] = {9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9};
    ZTEST_ASSERT_EQ(iSwapModel(m, w2, sizeof(w2), 2), ZHIO_OK);
    ZTEST_ASSERT_EQ(iVerifyModelChecksum(m), ZHIO_OK);
    ZTEST_ASSERT_EQ(eModelGetState(m), ZHIO_MODEL_READY);
}

/* ---------- 推理调度器（EDF） ---------- */
static void test_scheduler_edf(void)
{
    iInferenceSchedulerInit();
    int t1 = xCreateInferenceTask("t1", 1, 100, 100, NULL);
    int t2 = xCreateInferenceTask("t2", 1, 50, 100, NULL);   /* 同优先级，更早截止期 */
    int t3 = xCreateInferenceTask("t3", 3, 1000, 1000, NULL); /* 更高优先级 */
    ZTEST_ASSERT(t1 >= 0 && t2 >= 0 && t3 >= 0);

    xInferenceTaskActivate(t1);
    xInferenceTaskActivate(t2);
    xInferenceTaskActivate(t3);

    /* t3 优先级最高，先被选中 */
    int next = -1;
    ZTEST_ASSERT_EQ(xInferenceSchedulerGetNext(&next), ZHIO_OK);
    ZTEST_ASSERT_EQ(next, t3);
    xInferenceTaskComplete(t3);

    /* t1/t2 同优先级，EDF 选 t2（截止期 50 < 100） */
    ZTEST_ASSERT_EQ(xInferenceSchedulerGetNext(&next), ZHIO_OK);
    ZTEST_ASSERT_EQ(next, t2);
    xInferenceTaskComplete(t2);

    ZTEST_ASSERT_EQ(xInferenceSchedulerGetNext(&next), ZHIO_OK);
    ZTEST_ASSERT_EQ(next, t1);
    xInferenceTaskComplete(t1);
}

/* ---------- 安全 ---------- */
static int reject_validator(ModelHandle_t m, TensorHandle_t i, TensorHandle_t o)
{ (void)m;(void)i;(void)o; return 1; }   /* 拒绝 */
static int allow_validator(ModelHandle_t m, TensorHandle_t i, TensorHandle_t o)
{ (void)m;(void)i;(void)o; return 0; }   /* 放行 */

static void test_security_safe_inference(void)
{
    iTensorPoolInit(0);
    iModelRuntimeInit();
    iSecurityInit();
    vNPUUnregisterAll(); iRegisterNPUDevice(xNPUGetHostSimDevice()); iNPUInit();
    static const uint8_t w[8]={0,0,0,0,0,0,0,0};
    ModelHandle_t m = xRegisterModel("m", ZHIO_MODEL_FMT_HOST_SIM, w, sizeof(w), 1);
    iLoadModel(m);
    TensorDesc_t d; memset(&d,0,sizeof(d)); d.dims=1; d.shape[0]=2; d.type=ZHIO_TENSOR_UINT8; d.size_bytes=2;
    TensorHandle_t ti=xAllocTensor(&d), to=xAllocTensor(&d);

    /* 绑定拒绝校验器 → 安全推理被拦截 */
    ZTEST_ASSERT_EQ(xSetSafetyValidator(m, reject_validator), ZHIO_OK);
    ZTEST_ASSERT_EQ(xSafeInference(m, ti, to, 100), ZHIO_E_SAFETY);
    /* 绑定放行校验器 → 通过 */
    ZTEST_ASSERT_EQ(xSetSafetyValidator(m, allow_validator), ZHIO_OK);
    ZTEST_ASSERT_EQ(xSafeInference(m, ti, to, 100), ZHIO_OK);
}

/* ---------- 通信帧协议 ---------- */
static void test_comm_frame(void)
{
    uint8_t enc[256]; uint16_t elen = 0;
    const uint8_t payload[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    ZTEST_ASSERT_EQ(comm_frame_encode(ZHIO_FRAME_CMD, payload, 4, enc, &elen), ZHIO_OK);
    ZhiosFrame_t f; memset(&f,0,sizeof(f));
    ZTEST_ASSERT_EQ(comm_frame_decode(enc, elen, &f), ZHIO_OK);
    ZTEST_ASSERT_EQ(f.type, (uint8_t)ZHIO_FRAME_CMD);
    ZTEST_ASSERT_EQ(f.len, 4u);
    ZTEST_ASSERT_EQ(memcmp(f.payload, payload, 4), 0);
    /* 篡改载荷 → CRC 失败 */
    enc[6] ^= 0xFF;
    ZTEST_ASSERT(comm_frame_decode(enc, elen, &f) != ZHIO_OK);
}

static void test_comm_crc(void)
{
    const char *s = "123456789";
    ZTEST_ASSERT_EQ(comm_crc32(s, 9), 0xCBF43926u);   /* 标准 CRC32 校验向量 */
}

void ztest_run_core(void)
{
    ZTEST_RUN(test_tensor_basic);
    ZTEST_RUN(test_tensor_oom);
    ZTEST_RUN(test_tensor_stats);
    ZTEST_RUN(test_model_lifecycle);
    ZTEST_RUN(test_scheduler_edf);
    ZTEST_RUN(test_security_safe_inference);
    ZTEST_RUN(test_comm_frame);
    ZTEST_RUN(test_comm_crc);
}
