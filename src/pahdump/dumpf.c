#include "dumpf.h"

#define min(a, b) (((a) < (b)) ? (a) : (b))

typedef unsigned char u8;
typedef u8 bool;

#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#define DUMP_PATH "/tmp/xxxxtmp.log"

int PAGE_SIZE;

int dumpf_fd;

dumpf_metablock meta_block;

usize dumpf_size()
{
    lseek(dumpf_fd, 0, SEEK_SET);
    u8 u = 0, v;
    int err;
    while ((err = read(dumpf_fd, &v, 1)) > 0)
        u++;
    return u;
}

void *dumpf_malloc()
{
    return malloc(PAGE_SIZE);
}

#define dumpf_free(blkmem) free(blkmem)

void dumpf_new_metablock(dumpf_metablock *meta)
{
    meta->magic0 = 0xd2df687cd82d3d5d;
    meta->magic1 = 0x2e0e3a6260818d7f;
    meta->blkmap = 0;
    meta->patree = 0;
}

void dumpf_sync_block_from_file(usize block_id, void *block, usize length)
{
    if (length == 0)
    {
        length = PAGE_SIZE;
    }
    lseek(dumpf_fd, block_id * PAGE_SIZE, SEEK_SET);
    read(dumpf_fd, block, min(PAGE_SIZE, length));
}

void dumpf_sync_block_to_file(usize block_id, void *block)
{
    lseek(dumpf_fd, block_id * PAGE_SIZE, SEEK_SET);
    write(dumpf_fd, block, sizeof(PAGE_SIZE));
}

void dumpf_blkmap_build()
{
    meta_block.blkmap = dumpf_size() / PAGE_SIZE;
    dumpf_mapblock *mapb_root = dumpf_malloc();
    mapb_root->base.wlock = 0;
    mapb_root->next_block = 0;
    mapb_root->block_limit = (PAGE_SIZE - sizeof(dumpf_mapblock)) * 8;
    mapb_root->block_count = 0;
    dumpf_sync_block_to_file(meta_block.blkmap, mapb_root);
    free(mapb_root);
}

#define dumpf_open_block(bid, blkmem)                   \
    do                                                  \
    {                                                   \
        dumpf_sync_block_from_file((bid), (blkmem), 0); \
    } while ((blkmem)->base.wlock);                     \
    (blkmem)->base.wlock = 1;                           \
    dumpf_sync_block_to_file(bid, blkmem);

#define dumpf_close_block(bid, blkmem) \
    blkmem->base.wlock = 0;            \
    dumpf_sync_block_to_file(bid, blkmem);

/**
 * @brief 请求一个新的块
 *
 * 在块地图中搜索一个未使用的块，没搜索到就接上一个新块
 *
 * @return usize 块id
 */
usize dumpf_alloc_block()
{
    dumpf_mapblock *mapb = dumpf_malloc();
    usize bid = meta_block.blkmap;
    dumpf_open_block(bid, mapb);
    bool succeeded = 0;
    usize count = 0;
    // 搜到页地图结束或遇到未使用块停止
    while (1)
    {
        for (usize i = 0; i < mapb->block_count; i++, count += 8)
        {
            if (mapb->map[i] != 0xff)
            {
                u8 x = mapb->map[i];
                u8 bit = 0;
                while (x & (1 << bit))
                    bit++;
                count += bit;
                succeeded = 1;
                break;
            }
        }
        if (!mapb->next_block)
        {
            break;
        }
        if (succeeded)
            break;
        dumpf_close_block(bid, mapb);
        bid = mapb->next_block;
        dumpf_open_block(bid, mapb);
    }
    // 已经搜索到块地图结束
    if (!succeeded)
    { // 如果这时页地图需要新块时需要添加一个新块
        if (mapb->block_count == mapb->block_limit)
        {
            usize newid = dumpf_size() / PAGE_SIZE;
            dumpf_mapblock *newmapb = dumpf_malloc();
            newmapb->block_count = 1;
            newmapb->block_limit = (PAGE_SIZE - sizeof(dumpf_mapblock)) * 8;
            newmapb->next_block = 0;
            newmapb->map[0] |= 1;
            dumpf_close_block(newid, newmapb);
            dumpf_free(newmapb);
            mapb->next_block = newid;
            dumpf_close_block(bid, mapb);
            dumpf_open_block(newid, mapb);
            count++;
        }
    }
    // 标记对应位并同步至文件中
    count %= mapb->block_limit;
    mapb->map[count / 8] |= 1 << (count % 8);
    dumpf_close_block(bid, mapb);
    dumpf_free(mapb);
    return count;
}

/**
 * @brief 初始化dumpf
 */
void dumpf_init()
{
    int err;
    struct stat st;
    err = stat(DUMP_PATH, &st);
    if (err < 0)
    {
        creat(DUMP_PATH, 0600); // 只有root可以读写的文件
    }
    dumpf_fd = open(DUMP_PATH, O_RDWR);
    // 读取或建立元数据块并同步
    if (err < 0)
    {
        dumpf_new_metablock(&meta_block);
        write(dumpf_fd, &meta_block, sizeof(dumpf_metablock));
    }
    else
    {
        read(dumpf_fd, &meta_block, sizeof(dumpf_metablock));
        if (meta_block.magic0 != 0xd2df687cd82d3d5d || meta_block.magic1 != 0x2e0e3a6260818d7f)
        { // 魔数无法对应，重建dumpf
            dumpf_new_metablock(&meta_block);
            dumpf_sync_block_to_file(0, &meta_block);
            close(dumpf_fd);
            truncate(DUMP_PATH, PAGE_SIZE);
            dumpf_fd = open(DUMP_PATH, O_RDWR);
        }
    }
    dumpf_sync_block_from_file(dumpf_fd, &meta_block, sizeof(dumpf_metablock));
    if (!meta_block.blkmap)
    {
        dumpf_blkmap_build();
    }
}

void init_pahdump()
{
    PAGE_SIZE = sysconf(_SC_PAGESIZE);
    dumpf_init();
}

void save_page(unsigned char *page, addr_t phaddr)
{
}
