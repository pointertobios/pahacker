#include "memread.h"

#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>

int PAGE_SIZE;

void read_page(unsigned char *page, int pid, addr_t addr)
{
    for (int i = 0; i < PAGE_SIZE; i += sizeof(long))
    {
        long data = ptrace(PTRACE_PEEKDATA, pid, addr + i);
        memcpy(page + i, &data, sizeof(long));
    }
}
