#include <linux/fs.h>
#include <linux/dcache.h>
#include <linux/namei.h>
#include <linux/file.h>

static int modify_file_content(const char *file_path, const char *new_content)
{
    struct file *file;
    loff_t pos = 0;
    mm_segment_t old_fs;
    ssize_t ret;

    // 打开文件
    file = filp_open(file_path, O_WRONLY | O_CREAT, 0644);
    if (IS_ERR(file)) {
        pr_err("Failed to open file: %s\n", file_path);
        return PTR_ERR(file);
    }

    // 切换到内核态访问模式
    old_fs = get_fs();
    set_fs(KERNEL_DS);

    // 写入新内容
    ret = vfs_write(file, new_content, strlen(new_content), &pos);
    if (ret < 0) {
        pr_err("Failed to write to file: %s\n", file_path);
    } else {
        pr_info("Successfully wrote to file: %s\n", file_path);
    }

    // 恢复用户态访问模式
    set_fs(old_fs);

    // 关闭文件
    filp_close(file, NULL);
    return ret;
}
