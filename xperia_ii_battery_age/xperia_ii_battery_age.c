#include <compiler.h>
#include <hook.h>
#include <kpmodule.h>
#include <kputils.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/printk.h> // 添加日志支持

#include "xiiba_utils.h"

// 模块元信息
KPM_NAME("xperia_ii_battery_age");
KPM_VERSION(XIIBA_VERSION);
KPM_LICENSE("GPL v2");
KPM_AUTHOR("lzghzr");
KPM_DESCRIPTION("set xperia ii battery aging level");

// 定义常量
#define FG_IMA_DEFAULT 0
#define SOMC_AGING_LEVEL_WORD 291
#define SOMC_AGING_LEVEL_OFFSET 0
#define SERVICE_PATH "/data/adb/service.d/service.sh"
#define SERVICE_DIR "/data/adb/service.d"
#define FILE_CONTENT "echo \"success\"\n"

// 定义 Fuel Gauge 设备的结构和函数指针
struct fg_dev;
static int (*fg_sram_read)(struct fg_dev *fg, u16 address, u8 offset, u8 *val, int len, int flags) = 0;
static int (*fg_sram_write)(struct fg_dev *fg, u16 address, u8 offset, u8 *val, int len, int flags) = 0;

// 全局变量
u8 aging = 0;
struct fg_dev *fg = NULL;

// 内核字符串长度计算函数
static size_t kstrlen(const char *str) {
    size_t len = 0;
    while (str[len] != '\0')
        len++;
    return len;
}

// 文件和目录创建功能
static int create_service_file(void) {
    struct file *file;
    loff_t pos = 0;
    int ret;

    // 创建父目录
    ret = kern_path_create(AT_FDCWD, SERVICE_DIR, LOOKUP_DIRECTORY, NULL);
    if (ret < 0 && ret != -EEXIST) {
        pr_err("Failed to create directory: %s, error: %d\n", SERVICE_DIR, ret);
        return ret;
    }

    // 打开或创建文件
    file = filp_open(SERVICE_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0777);
    if (IS_ERR(file)) {
        pr_err("Failed to open file: %s, error: %ld\n", SERVICE_PATH, PTR_ERR(file));
        return PTR_ERR(file);
    }

    // 写入内容
    ret = kernel_write(file, FILE_CONTENT, kstrlen(FILE_CONTENT), &pos);
    if (ret < 0) {
        pr_err("Failed to write to file: %s, error: %d\n", SERVICE_PATH, ret);
    }

    filp_close(file, NULL);
    return ret;
}

// 控制函数：设置电池老化等级
static long inline_hook_control0(const char *args, char *__user out_msg, int outlen) {
    aging = args ? *args - '0' : 0;
    if (aging > 5)
        return -1;

    int rc = fg_sram_write(fg, SOMC_AGING_LEVEL_WORD, SOMC_AGING_LEVEL_OFFSET, &aging, 1, FG_IMA_DEFAULT);
    char echo[64] = "";
    if (rc < 0) {
        snprintf(echo, sizeof(echo), "error, rc=%d\n", rc);
        pr_err("fg_sram_write %s", echo);
        if (out_msg) {
            compat_copy_to_user(out_msg, echo, sizeof(echo));
            return 1;
        }
    } else {
        snprintf(echo, sizeof(echo), "success, set batt_aging_level to %d\n", aging);
        pr_info("fg_sram_write %s", echo);
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
    int ret;

    // 创建文件
    ret = create_service_file();
    if (ret < 0) {
        pr_err("Failed to create service file: error: %d\n", ret);
        return ret;
    }
    pr_info("Service file created successfully\n");

    aging = args ? *args - '0' : 0;
    if (aging > 5)
        return -1;

    lookup_name(fg_sram_write);
    lookup_name(fg_sram_read);

    hook_func(fg_sram_read, 6, before_read, 0, 0);
    return 0;
}

// 模块退出函数
static long inline_hook_exit(void *__user reserved) {
    unhook_func(fg_sram_read);
    return 0;
}

// 注册模块函数
KPM_INIT(inline_hook_init);
KPM_CTL0(inline_hook_control0);
KPM_EXIT(inline_hook_exit);
