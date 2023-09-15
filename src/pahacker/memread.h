#ifndef __MEMREAD_H__
#define __MEMREAD_H__ 1
#endif

typedef unsigned long long addr_t;
typedef unsigned char u8;

void read_page(unsigned char *page, int pid, addr_t addr);

#undef __MEMREAD_H__
