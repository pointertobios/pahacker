#include "dumpf.h"

#define min(a, b) (((a) < (b)) ? (a) : (b))

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
    dumpf_sync_block_to_file(0, &meta_block);
    dumpf_mapblock *mapb_root = dumpf_malloc();
    mapb_root->base.wlock = 0;
    mapb_root->base.next_block = 0;
    mapb_root->block_limit = (PAGE_SIZE - sizeof(dumpf_mapblock)) * 8;
    mapb_root->block_count = 0;
    dumpf_sync_block_to_file(meta_block.blkmap, mapb_root);
    free(mapb_root);
}

#define dumpf_open_block(bid, blkmem)                       \
    {                                                       \
        do                                                  \
        {                                                   \
            dumpf_sync_block_from_file((bid), (blkmem), 0); \
        } while ((blkmem)->base.wlock);                     \
        (blkmem)->base.wlock = 1;                           \
        dumpf_sync_block_to_file(bid, blkmem);              \
    }

#define dumpf_close_block(bid, blkmem)         \
    {                                          \
        blkmem->base.wlock = 0;                \
        dumpf_sync_block_to_file(bid, blkmem); \
    }

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
        if (!mapb->base.next_block)
        {
            break;
        }
        if (succeeded)
            break;
        dumpf_close_block(bid, mapb);
        bid = mapb->base.next_block;
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
            newmapb->base.next_block = 0;
            newmapb->map[0] |= 1;
            dumpf_close_block(newid, newmapb);
            dumpf_free(newmapb);
            mapb->base.next_block = newid;
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

void dumpf_addrtree_build()
{
    usize bid = meta_block.patree = dumpf_alloc_block();
    dumpf_sync_block_to_file(0, &meta_block);
    dumpf_addrtreeblock *patreeb = dumpf_malloc();
    patreeb->base.wlock = 0;
    patreeb->base.next_block = 0;
    patreeb->length = 0;
    patreeb->available = DUMPF_ADDRTREEBLK_AVALABLE;
    patreeb->start_nid = 0;
    dumpf_sync_block_to_file(bid, patreeb);
    dumpf_free(patreeb);
}

/**
 * @brief 将地址二叉树的储存容量增大一个块
 *
 */
void dumpf_addrtree_extend()
{
    usize bid = dumpf_alloc_block();
    dumpf_addrtreeblock *patb = dumpf_malloc();
    usize id = meta_block.patree;
    dumpf_open_block(id, patb);
    while (patb->base.next_block)
    {
        dumpf_close_block(id, patb);
        id = patb->base.next_block;
        dumpf_open_block(id, patb);
    }
    patb->base.next_block = bid;
    dumpf_close_block(id, patb);
    memset(patb, 0, PAGE_SIZE);
    patb->base.wlock = 0;
    patb->base.next_block = 0;
    patb->length = 0;
    patb->available = DUMPF_ADDRTREEBLK_AVALABLE;
    patb->start_nid += patb->available;
    dumpf_close_block(bid, patb);
    dumpf_free(patb);
}

/**
 * @brief 为地址二叉树分配一个新节点
 *
 * @return usize
 */
usize dumpf_addrtree_alloc_node()
{
    usize bid = meta_block.patree;
    dumpf_addrtreeblock *patb = dumpf_malloc();
    dumpf_open_block(bid, patb);
    usize nid = 0;
    bool succeeded = 0;
    while (1)
    {
        for (; (nid - patb->start_nid) < patb->length; nid++)
        {
            if (!patb->addrtree[nid - patb->start_nid].available)
            {
                patb->addrtree[nid - patb->start_nid].available = 1;
                succeeded = 1;
                break;
            }
        }
        if (succeeded)
            break;
        if (!patb->base.next_block)
            break;
        dumpf_close_block(bid, patb);
        bid = patb->base.next_block;
        dumpf_open_block(bid, patb);
    }
    if (succeeded)
    {
        dumpf_close_block(bid, patb);
        dumpf_free(patb);
        return nid;
    }
    if (patb->length == patb->available)
    {
        dumpf_close_block(bid, patb);
        dumpf_addrtree_extend();
        dumpf_open_block(bid, patb);
        bid = patb->base.next_block;
        dumpf_open_block(bid, patb);
    }
    patb->length++;
    patb->addrtree[nid - patb->start_nid].available = 1;
    dumpf_close_block(bid, patb);
    dumpf_free(patb);
    return nid;
}

/**
 * @brief 获取地址二叉树的一个节点
 *
 * @param nid
 * @param blk 需自己分配内存使用
 * @param _bid 需自己分配内存使用
 * @return addrp_node*
 */
addrp_node *dumpf_addrtree_getnode(usize nid, dumpf_addrtreeblock *blk, usize *_bid)
{
    usize bid = meta_block.patree;
    dumpf_addrtreeblock *patb = dumpf_malloc();
    dumpf_open_block(bid, patb);
    for (usize i = 1; i < nid / DUMPF_ADDRTREEBLK_AVALABLE; i++)
    {
        dumpf_close_block(bid, patb);
        bid = patb->base.next_block;
        dumpf_open_block(bid, patb);
    }
    memcpy(blk, patb, PAGE_SIZE);
    dumpf_free(patb);
    *_bid = bid;
    return &(blk->addrtree[nid - blk->start_nid]);
}

#define dumpf_addrtree_node_set(nid, memb, val)              \
    {                                                        \
        dumpf_addrtreeblock *blk = dumpf_malloc();           \
        usize *__bid = malloc(sizeof(usize));                \
        dumpf_addrtree_getnode(nid, blk, __bid)->memb = val; \
        dumpf_close_block(*__bid, blk);                      \
        dumpf_free(blk);                                     \
        free(__bid);                                         \
    }

#define dumpf_addrtree_node_get(nid, memb, val)               \
    {                                                         \
        dumpf_addrtreeblock *blk = dumpf_malloc();            \
        usize *__bid = malloc(sizeof(usize));                 \
        *val = dumpf_addrtree_getnode(nid, blk, __bid)->memb; \
        dumpf_close_block(*__bid, blk);                       \
        dumpf_free(blk);                                      \
        free(__bid);                                          \
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
    if (!meta_block.patree)
    {
        dumpf_addrtree_build();
    }
}

void init_pahdump()
{
    PAGE_SIZE = sysconf(_SC_PAGESIZE);
    dumpf_init();
}

void save_page(unsigned char *page, addr_t phaddr)
{
    bool root_node_avai;
    dumpf_addrtree_node_get(0, available, &root_node_avai);
    if (!root_node_avai)
    {
        dumpf_addrtree_alloc_node();
        dumpf_addrtree_node_set(0, left, 0);
        dumpf_addrtree_node_set(0, right, 0);
        dumpf_addrtree_node_set(0, pair.addr, phaddr);
        usize bid = dumpf_alloc_block();
        dumpf_addrtree_node_set(0, pair.block, bid);
        dumpf_sync_block_to_file(bid, page);
        return;
    }
    usize nid = 0;
    usize left, right;
    while (left || right)
    {
        addr_t nad;
        dumpf_addrtree_node_get(nid, left, &left);
        dumpf_addrtree_node_get(nid, right, &right);
        dumpf_addrtree_node_get(nid, pair.addr, &nad);
        if (phaddr < nad && !left)
        {
            usize id = dumpf_addrtree_alloc_node();
            dumpf_addrtree_node_set(nid, left, id);

            dumpf_addrtree_node_set(id, left, 0);
            dumpf_addrtree_node_set(id, right, 0);
            dumpf_addrtree_node_set(id, pair.addr, phaddr);
            usize bid = dumpf_alloc_block();
            dumpf_addrtree_node_set(id, pair.block, bid);
            dumpf_sync_block_to_file(bid, page);
            return;
        }
        else if (phaddr > nad && !right)
        {
            usize id = dumpf_addrtree_alloc_node();
            dumpf_addrtree_node_set(nid, right, id);

            dumpf_addrtree_node_set(id, left, 0);
            dumpf_addrtree_node_set(id, right, 0);
            dumpf_addrtree_node_set(id, pair.addr, phaddr);
            usize bid = dumpf_alloc_block();
            dumpf_addrtree_node_set(id, pair.block, bid);
            dumpf_sync_block_to_file(bid, page);
            return;
        }
        else if (phaddr == nad)
        {
            usize bid;
            dumpf_addrtree_node_get(nid, pair.block, &bid);
            dumpf_sync_block_to_file(bid, page);
            return;
        }

        if (phaddr < nad)
        {
            nid = left;
        }
        else if (phaddr > nad)
        {
            nid = right;
        }
    }
}
