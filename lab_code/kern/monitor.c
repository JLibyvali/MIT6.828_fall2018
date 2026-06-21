// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include "inc/mmu.h"
#include "inc/types.h"

#include <inc/assert.h>
#include <inc/memlayout.h>
#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/x86.h>
#include <kern/console.h>
#include <kern/kdebug.h>
#include <kern/monitor.h>
#include <kern/pmap.h>

#define CMDBUF_SIZE 80  // enough for one VGA text line

struct Command
{
    const char *name;
    const char *desc;
    // return -1 to force monitor to exit
    int (*func)(int argc, char **argv, struct Trapframe *tf);
};

static struct Command commands[] = {
    {"help", "Display this list of commands", mon_help},
    {"kerninfo", "Display information about the kernel", mon_kerninfo},
    {"backtrace", "Monitor print stack backtrace", mon_backtrace},
    {"showmappings", "Display the VA address range mapped physical pages and permission bits", mon_showmappings},
};

/***** Implementations of basic kernel monitor commands *****/

int mon_help(int argc, char **argv, struct Trapframe *tf)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(commands); i++)
        cprintf("%s - %s\n", commands[i].name, commands[i].desc);
    return 0;
}

int mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
    extern char _start[], entry[], etext[], edata[], end[];

    cprintf("Special kernel symbols:\n");
    cprintf("  _start                  %08x (phys)\n", _start);
    cprintf("  entry  %08x (virt)  %08x (phys)\n", entry, entry - KERNBASE);
    cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
    cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
    cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
    cprintf("Kernel executable memory footprint: %dKB\n", ROUNDUP(end - entry, 1024) / 1024);
    return 0;
}

int mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
    // Your code here.
    // And because GCC optimization, `read_ebp` such inline functions may get calls before `mon_backtrace` function
    // prologue, So we need assembly code re-check.

    uint32_t *esp = (uint32_t *)read_esp();
    uint32_t *ebp = (uint32_t *)read_ebp();
    cprintf(ANSI_BLUE "Stack backtrace" ANSI_NONE
    );  // The `mon_backtrace()` itself, and we need print all outstanding stack frames.

    /* When call functions, the `call` instruction will push function return address to stack.
            So at the function entry just after the `call`. *%esp points to the return address
            And *(%esp + 4) points to first argument pushed by caller.
    */
    /*
            And then function prologue will:
                    1. Push save caller's stack frame base pointer to stack.
                    2. Save this function current stack frame base pointer to $ebp register.
            And with function execution order, $esp will keep substraction moving.
            But $ebp always remember this stack frame entry beginning.
    */
    int args[5];  // Here we need print first 5 arguments.
    while ((uint32_t)ebp != 0)
    {
        memset(args, 0, 5);
        uint32_t callerEbp = *ebp;
        uint32_t ret       = *(ebp + 1);

        for (int i = 0; i < 5; i++)
        {
            args[i] = *(ebp + 2 + i);  // Skip caller's $ebp and return address.
        }

        struct Eipdebuginfo info;
        memset(&info, 0, sizeof(struct Eipdebuginfo));
        if (debuginfo_eip(ret, &info) == 0)
        {
            cprintf(
                "ebp %08x eip %08x args %08x %08x %08x %08x %08x\n", (uint32_t)ebp, ret, args[0], args[1], args[2],
                args[3], args[4]
            );
            cprintf(
                "%s:%d: %.*s+%d\n", info.eip_file, info.eip_line, info.eip_fn_namelen, info.eip_fn_name,
                (ret - info.eip_fn_addr)
            );
        }
        ebp = (uint32_t *)callerEbp;
    }

    return 0;
}

int mon_showmappings(int argc, char **argv, struct Trapframe *tf)
{
    const char *helpStr = "Usage: showmappings <the Start virtual address> <the End virtual address>\n";
    if (argc != 3)
    {
        cprintf(helpStr);
        return -1;
    }

    long start = strtol(argv[1], NULL, 16);
    long end   = strtol(argv[2], NULL, 16);
    if (start > end)
    {
        cprintf(helpStr);
        return -1;
    }

    long curStart = start;
    cprintf("Offset\t\tVA\t\tPTE\t\tPA\t\tPTE_P\t\tPTE_W\t\tPTE_U\n");
    for (int i = 0; curStart < end; i += PGSIZE)
    {
        curStart = start + i;

        pte_t           *pte;
        struct PageInfo *pp = page_lookup(kern_pgdir, (void *)curStart, &pte);
        if (pp != NULL)
        {
            physaddr_t pteEntry  = *pte;
            physaddr_t pagePA    = page2pa(pp);
            uint32_t   offsetIdx = i / PGSIZE;
            int        perm_p    = (pteEntry & PTE_P) ? 1 : 0;
            int        perm_w    = (pteEntry & PTE_W) ? 1 : 0;
            int        perm_u    = (pteEntry & PTE_U) ? 1 : 0;
            cprintf(
                "%d\t\t%08x\t\t%08x\t\t%08x\t\t%-6d\t\t%-6d\t\t%-6d\t\t\n", offsetIdx, curStart, pteEntry, pagePA,
                perm_p, perm_w, perm_u
            );
        }
    }

    return 0;
}

int mon_setperm(int argc, char **argv, struct Trapframe *tf)
{
    const char *helpStr = "Usage: setperm <virtual address> <P|W|U> <0|1>\n";
    if (argc != 4)
    {
        cprintf(helpStr);
        return -1;
    }

    long  va       = strtol(argv[1], NULL, 16);
    char *permFlag = argv[2];
    long  enabled  = strtol(argv[3], NULL, 16);

    if (strcmp(permFlag, "P") && strcmp(permFlag, "W") && strcmp(permFlag, "U"))
    {
        cprintf(helpStr);
        return -1;
    }

    if (!strcmp("P", permFlag))
    {
        pte_t *pte;
        if (page_lookup(kern_pgdir, (void *)va, &pte) != NULL)
        {
            if (enabled)
            {
                *pte |= PTE_P;
            }
            else
            {
                *pte &= ~PTE_P;
            }
        }
    }
    else if (!strcmp("W", permFlag))
    {
        pte_t *pte;
        if (page_lookup(kern_pgdir, (void *)va, &pte) != NULL)
        {
            if (enabled)
            {
                *pte |= PTE_W;
            }
            else
            {
                *pte &= ~PTE_W;
            }
        }
    }
    else if (!strcmp("U", permFlag))
    {
        pte_t *pte;
        if (page_lookup(kern_pgdir, (void *)va, &pte) != NULL)
        {
            if (enabled)
            {
                *pte |= PTE_U;
            }
            else
            {
                *pte &= ~PTE_U;
            }
        }
    }

    tlb_invalidate(kern_pgdir, (void *)va);
    return 0;
}

int mon_dumpMemory(int argc, char **argv, struct Trapframe *tf)
{
    const char *helpStr = "Usage: dumpMemory <va|pa> <address start> <address end>\n";
    if (argc != 4)
    {
        cprintf(helpStr);
        return -1;
    }

	long startAddr = strtol(argv[2], NULL, 16);
	long endAddr = strtol(argv[2], NULL, 16);
	if(startAddr < endAddr)
	{
		cprintf(helpStr);
		return -1;
	}

    if (!strcmp(argv[1], "va"))
    {
		long curStart = startAddr;
    }
    else if (!strcmp(argv[1], "pa"))
    {
    }
    else
    {
        cprintf(helpStr);
        return -1;
    }

    return 0;
}

/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS    16

static int runcmd(char *buf, struct Trapframe *tf)
{
    int   argc;
    char *argv[MAXARGS];
    int   i;

    // Parse the command buffer into whitespace-separated arguments
    argc       = 0;
    argv[argc] = 0;
    while (1)
    {
        // gobble whitespace
        while (*buf && strchr(WHITESPACE, *buf))
            *buf++ = 0;
        if (*buf == 0)
            break;

        // save and scan past next arg
        if (argc == MAXARGS - 1)
        {
            cprintf("Too many arguments (max %d)\n", MAXARGS);
            return 0;
        }
        argv[argc++] = buf;
        while (*buf && !strchr(WHITESPACE, *buf))
            buf++;
    }
    argv[argc] = 0;

    // Lookup and invoke the command
    if (argc == 0)
        return 0;
    for (i = 0; i < ARRAY_SIZE(commands); i++)
    {
        if (strcmp(argv[0], commands[i].name) == 0)
            return commands[i].func(argc, argv, tf);
    }
    cprintf("Unknown command '%s'\n", argv[0]);
    return 0;
}

void monitor(struct Trapframe *tf)
{
    char *buf;

    cprintf(ANSI_GREEN "Welcome to the JOS kernel monitor!" ANSI_NONE);
    cprintf("Type 'help' for a list of commands.\n");

    while (1)
    {
        buf = readline("K> ");
        if (buf != NULL)
            if (runcmd(buf, tf) < 0)
                break;
    }
}
