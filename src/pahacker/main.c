#include "vad2pad.h"
#include "memread.h"
#include "../pahdump/dumpf.h"
#include <sys/stat.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>

extern int PAGE_SIZE;

/**
 * @brief scan_pages的参数
 *
 * pid - 进程号
 * pagemap_fd - 页表文件
 * addr - 需要扫描的虚拟地址空间的页框起始地址
 * page_count - 需要扫描的数量
 *
 */
typedef struct
{
    int pid;
    int pagemap_fd;
    addr_t addr;
    int page_count;
} scan_pages_args_t;

/**
 * @brief 多线程加速扫描
 *
 * @param _args
 */
void *scan_pages(void *_args)
{
    scan_pages_args_t *args = (scan_pages_args_t *)_args;
    u8 *page = malloc(PAGE_SIZE);
    while (args->page_count--)
    {
        PagemapEntry entry;
        pagemap_get_entry(&entry, args->pagemap_fd, args->addr); // 获取地址信息
        if (entry.present && !entry.swapped)                     // 页存在且没被交换
        {
            addr_t phaddr = entry.pfn * PAGE_SIZE;
            read_page(page, args->pid, args->addr);
            save_page(page, entry.pfn * PAGE_SIZE);
        }
        args->addr += PAGE_SIZE;
    }
}

/**
 * @brief 扫描进程
 *
 * 需要扫描的虚拟内存空间区域:
 *  0x0000550000000000 ~ 0x0000560000000000
 *  0x00007f0000000000 ~ 0x0000800000000000
 *
 * @param pid
 */
void scan(unsigned short pid)
{
    if (pid == getpid()) // 不扫描自己
        return;
    char fname[64];
    sprintf(fname, "/proc/%d/", pid);
    struct stat st;
    int r = stat(fname, &st);
    if (r) // 进程目录不存在时跳过
        return;
    sprintf(fname, "/proc/%d/pagemap", pid);
    int fd = open(fname, O_RDONLY);
    if (fd < 0) // 进程页表打开失败就跳过
        return;
    unsigned int cpunum = sysconf(_SC_NPROCESSORS_ONLN); // 获取cpu数量
    cpunum &= ~7;
    // attach到进程
    ptrace(PTRACE_ATTACH, pid, NULL, NULL);
    // 第一段地址空间
    int loo = cpunum;
    addr_t pfn = 0x0000550000000000 / PAGE_SIZE;
    int step = (0x0000560000000000 / PAGE_SIZE - pfn) / cpunum;
    while (loo--)
    {
        scan_pages_args_t scan_pages_args = {
            .pid = pid,
            .pagemap_fd = fd,
            .addr = pfn * PAGE_SIZE,
            .page_count = step,
        };
        pfn += step;
        pthread_create(NULL, NULL, scan_pages, (void *)&scan_pages_args);
    }
    // 第二段地址空间
    loo = cpunum;
    pfn = 0x00007f0000000000 / PAGE_SIZE;
    step = (0x0000800000000000 / PAGE_SIZE - pfn) / cpunum;
    while (loo--)
    {
        scan_pages_args_t scan_pages_args = {
            .pid = pid,
            .pagemap_fd = fd,
            .addr = pfn * PAGE_SIZE,
            .page_count = step,
        };
        pfn += step;
        pthread_create(NULL, NULL, scan_pages, (void *)&scan_pages_args);
    }
    // 脱离进程
    ptrace(PTRACE_DETACH, pid, NULL, NULL);
}

int main()
{
    setuid(0);
    init_pahdump();
    PAGE_SIZE = sysconf(_SC_PAGESIZE);
    for (unsigned short pid = 1;; pid++)
    {
        if (pid == 0)
            continue;
        scan(pid);
    }
    return 0;
}
