/*
 * test_main.c - ZTEST 测试运行器
 *
 * 对应《23-验证报告》。主机运行：初始化系统后执行全部单元测试，
 * 汇总统计并以退出码表示结果（0=全通过）。
 */
#include <stdio.h>
#include "ztest.h"
#include "zhios.h"

void ztest_run_core(void);
void ztest_run_agent(void);

int main(void)
{
    printf("=== ZhiOS-AF ZTEST Runner ===\n");
    int rc = zhio_system_init();
    printf("system init rc=%d\n", rc);

    ztest_run_core();
    ztest_run_agent();

    printf("\n=== ZTEST SUMMARY ===\n");
    printf("tests   : %d\n", ztest_tests);
    printf("checks  : %d\n", ztest_checks);
    printf("failures: %d\n", ztest_failures);
    if (ztest_failures == 0) {
        printf("RESULT  : ALL PASS\n");
        return 0;
    }
    printf("RESULT  : FAILURES DETECTED\n");
    return 1;
}
