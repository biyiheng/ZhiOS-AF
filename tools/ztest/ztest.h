/*
 * ztest.h - 轻量级单元测试框架（ZTEST）
 *
 * 对应《22-压力测试方案》《23-验证报告》。
 * 主机可运行；宏收集断言与用例统计，供 test 运行器汇总。
 */
#ifndef ZTEST_H
#define ZTEST_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 统计计数器（由 ztest.c 定义） */
extern int ztest_checks;
extern int ztest_failures;
extern int ztest_tests;

#define ZTEST_PASS 0
#define ZTEST_FAIL 1

/* 断言：失败打印并累计失败数 */
#define ZTEST_ASSERT(cond)                                                       \
    do {                                                                         \
        ztest_checks++;                                                          \
        if (!(cond)) {                                                           \
            printf("  [FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond);           \
            ztest_failures++;                                                    \
        }                                                                        \
    } while (0)

#define ZTEST_ASSERT_EQ(a, b)                                                    \
    do {                                                                         \
        ztest_checks++;                                                          \
        long long _a = (long long)(a), _b = (long long)(b);                      \
        if (_a != _b) {                                                          \
            printf("  [FAIL] %s:%d  %s(%lld) != %s(%lld)\n", __FILE__, __LINE__, \
                   #a, _a, #b, _b);                                              \
            ztest_failures++;                                                    \
        }                                                                        \
    } while (0)

#define ZTEST_ASSERT_NOT_NULL(p)                                                 \
    do {                                                                         \
        ztest_checks++;                                                          \
        if ((p) == NULL) {                                                       \
            printf("  [FAIL] %s:%d  %s is NULL\n", __FILE__, __LINE__, #p);      \
            ztest_failures++;                                                    \
        }                                                                        \
    } while (0)

/* 用例包裹：统计用例数 */
#define ZTEST_TEST(name) void name(void)
#define ZTEST_RUN(name)                                                          \
    do {                                                                         \
        ztest_tests++;                                                           \
        printf("[TEST] %s\n", #name);                                            \
        name();                                                                  \
    } while (0)

#ifdef __cplusplus
}
#endif

#endif /* ZTEST_H */
