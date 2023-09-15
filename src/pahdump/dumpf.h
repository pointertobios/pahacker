#ifndef __DUMPF_H__
#define __DUMPF_H__ 1
#endif

typedef unsigned long long addr_t;
typedef unsigned long long usize;
typedef unsigned char u8;

/**
 * dump文件格式
 *
 * dump文件是用于描述本机物理内存内容以及分布的文件格式。
 *
 * dump文件大小一定是PAGE_SIZE的整数倍，
 * 以PAGE_SIZE长度为单位分割，一个单位称为一个块。
 *
 * 每一个块都有一个id,id从0开始依次往后排。
 *
 * 物理地址管理使用平衡二叉树实现。
 */

/**
 * @brief 元数据块
 *
 */
typedef struct
{
    // 0xd2df687cd82d3d5d
    usize magic0;
    // 0x2e0e3a6260818d7f
    usize magic1;

    // 块地图的首个块
    usize blkmap;
    // 物理地址二叉树的首个块
    usize patree;
} dumpf_metablock;

/**
 * @brief 块地图块
 *
 */
typedef struct
{
    usize next_block;
    u8 wlock;
    u8 map[0];
} dumpf_mapblock;

/**
 * @brief 地址对
 *
 * 在物理地址二叉树中表示一个物理页框中的数据所在的块
 */
typedef struct
{
    addr_t addr;
    usize block;
} addr_pair;

/**
 * @brief 平衡二叉树节点
 *
 * 由于数据结构存在文件中，使用数组构建二叉树会增大磁盘io，
 * 这里使用节点/指针式二叉树
 *
 * left和right都是节点指针，
 * 其代表节点在二叉树块序列中addrtree总数组中的位置（从1开始）。
 */
typedef struct
{
    addr_pair pair;
    usize left, right;
} addrp_node;

/**
 * @brief 物理地址二叉树
 *
 * 使用绝对平衡二叉树
 */
typedef struct
{
    usize next_block;
    u8 wlock;
    usize length;    // 可以容纳的addrp_node总数
    usize available; // 有效的addrp_node个数
    addrp_node addrtree[0];
} dumpf_addrtreeblock;

void init_pahdump();

void save_page(unsigned char *page, addr_t phaddr);

#undef __DUMPF_H__
