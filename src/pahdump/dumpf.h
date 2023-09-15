#ifndef __DUMPF_H__
#define __DUMPF_H__ 1
#endif

typedef unsigned long long addr_t;

void save_page(unsigned char *page, addr_t phaddr);

#undef __DUMPF_H__
