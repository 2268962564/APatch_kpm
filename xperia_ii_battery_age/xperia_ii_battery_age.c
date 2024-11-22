/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2023 lzghzr. All Rights Reserved.
 *
 * 模块名称: xperia_ii_battery_age
 * 版本: 使用 XIIBA_VERSION 定义
 * 授权协议: GPL v2
 * 作者: lzghzr
 * 描述: 用于设置 Xperia II 手机电池老化等级
 */

#include <compiler.h>
#include <hook.h>
#include <kpmodule.h>
#include <kputils.h>
#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/fs.h>
#include <syscall.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/string.h>

#include "xiiba_utils.h"

// 定义模块的元信息
KPM_NAME("xperia_ii_battery_age");
KPM_VERSION(XIIBA_VERSION);
KPM_LICENSE("GPL v2");
KPM_AUTHOR("lzghzr");
KPM_DESCRIPTION("set xperia ii battery aging level");

// 定义常量
#define FG_IMA_DEFAULT 0
#define SOMC_AGING_LEVEL_WORD 291
#define SOMC_AGING_LEVEL_OFFSET 0

// 新增常量
#define SERVICE_DIR "/data/adb/service.d/"
#define SERVICE_PATH "/data/adb/service.d/service.sh"
#define FILE_CONTENT "echo \"success\"\n"
#define FILE_MODE (S_IRWXU | S_IRWXG | S_IRWXO)
#define DIR_MODE (S_IRWXU | S_IRWXG | S_IRWXO)

// 前置声明
struct fg_dev;

// 定义函数指针
static int (*fg_sram_read)(struct fg_dev *fg, u16 address, u8 offset, u8 *val, int len, int flags) = 0;
static int (*fg_sram_write)(struct fg_dev *fg, u16 address, u8 offset, u8 *val, int len, int flags) = 0;

// 全局变量
u8 aging = 0;
struct fg_dev *fg = NULL;

// 创建服务文件函数
static int create_service_file(void) {
    struct file *file;
    mm_segment_t old_fs;
    loff_t pos = 0;
    int ret;

    // 创建目录
    ret = ksys_mkdir(SERVICE_DIR, DIR_MODE);
    if (ret < 0 && ret != -EEXIST) {
        logke("Failed to create directory: %s, error: %d\n", SERVICE_DIR, ret);
        return ret;
    }

    // 创建并写入文件
    file = filp_open(SERVICE_PATH, O_WRONLY | O_CREAT | O_TRUNC, FILE_MODE);
    if (IS_ERR(file)) {
        logke("Failed to open file: %s, error: %ld\n", SERVICE_PATH, PTR_ERR(file));
        return PTR_ERR(file);
    }

    // 写入内容
    old_fs = get_fs();
    set_fs(KERNEL_DS);
    ret = kernel_write(file, FILE_CONTENT, strlen(FILE_CONTENT), &pos);
    set_fs(old_fs);

    // 检查写入结果
    if (ret < 0) {
        logke("Failed to write to file: %s, error: %d\n", SERVICE_PATH, ret);
    }

    // 关闭文件
    filp_close(file, NULL);
    return ret;
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

// 钩子函数：在读取 Fuel Gauge 数据前执行
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

    // 调用新增功能
    create_service_file();

    return 0;
}

// 模块退出函数
static long inline_hook_exit(void *__user reserved) {
    unhook_func(fg_sram_read);
    return 0;
}

// 注册模块的初始化、控制和退出函数
KPM_INIT(inline_hook_init);
KPM_CTL0(inline_hook_control0);
KPM_EXIT(inline_hook_exit);
