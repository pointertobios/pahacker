#include "dumpf.h"

#include <unistd.h>

#define DUMP_PATH "/tmp/xxxxtmp.log"

int PAGE_SIZE;

void init_pahdump()
{
    PAGE_SIZE = sysconf(_SC_PAGESIZE);
}

void save_page(unsigned char *page, addr_t phaddr)
{}
