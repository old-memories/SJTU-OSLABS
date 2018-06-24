// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>

#define CMDBUF_SIZE	80	// enough for one VGA text line


struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
	{"backtrace", "Display backtrace", mon_backtrace},
	{"time", "Display cpu cycles", mon_time},
	{"showmappings","Display the physical page mappings", mon_showmappings},
};
#define NCOMMANDS (sizeof(commands)/sizeof(commands[0]))

unsigned read_eip();

/***** Implementations of basic kernel monitor commands *****/

int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < NCOMMANDS; i++)
		cprintf("%s - %s\n", commands[i].name, commands[i].desc);
	return 0;
}

int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char entry[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  entry  %08x (virt)  %08x (phys)\n", entry, entry - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
		(end-entry+1023)/1024);
	return 0;
}



int
mon_time(int argc, char **argv, struct Trapframe *tf)
{
	// Your code here.
	unsigned long long start,end;
	int i;
	if(argc<2)
		return 0;
	for(i=0; i<NCOMMANDS;i++){
		if(strcmp(argv[1],commands[i].name)==0){
			start = read_tsc();
			commands[i].func(argc-1,argv+1,tf);
			end = read_tsc();
			break;
		}
	}
	if(i<NCOMMANDS){
		cprintf("%s cycles: %d\n",argv[1],end-start);
	}
	else{
		cprintf("Unknown command '%s'\n", argv[1]);
	}
	return 0;
}

extern pde_t *kern_pgdir;

/*
PDE(001) 00000000-00400000 00400000 urw
 |-- PTE(000008) 00200000-00208000 00008000 urw
PDE(001) 00800000-00c00000 00400000 urw
 |-- PTE(000006) 00800000-00806000 00006000 urw
*/
int
mon_showmappings(int argc, char **argv, struct Trapframe *tf)
{
	char flag[1<<8] = {
		[0] = '-',
		[PTE_P] = 'r',
		[PTE_W] = 'w',
		[PTE_U] = 'u',
		[PTE_PS] = 's',
	};
	if(argc<3)
		return 0;
		char *endptr;
	uintptr_t va_low = (uintptr_t)strtol(argv[1],&endptr,16);
	if(*endptr){
		cprintf("arg1's format error: %s.\n",argv[1]);
		return 0;
	}
	uintptr_t va_high = (uintptr_t)strtol(argv[2],&endptr,16);
	if(*endptr){
		cprintf("arg2's format error: %s.\n",argv[2]);
		return 0;
	}
	if(va_low>va_high){
		cprintf("arg1: %08x is larger than arg2: %08x.\n",va_low, va_high);
		return 0;
	}
	cprintf("show mappings: %08x - %08x\n",va_low,va_high);
	uintptr_t va_cur = va_low;
	pde_t *pgdir = kern_pgdir;//(pde_t *)PGADDR(PDX(UVPT),PDX(UVPT),0);
	//cprintf("kern_pgdir equals to %08x\n",(uint32_t)(PGADDR(PDX(UVPT),PDX(UVPT),0)));

	while(va_cur<=va_high && va_cur<=0xffffffff){
		pde_t pde = pgdir[PDX(va_cur)];
		if(pde & PTE_P){
			cprintf("PDE");
			cprintf("(%03x) %08x-%08x %08x %c%c%c%c", PDX(va_cur),va_cur,va_cur+PTSIZE-1,PTSIZE,flag[pde&PTE_P],flag[pde&PTE_W],flag[pde&PTE_U],flag[pde&PTE_PS]);
			if(pde & PTE_PS){
				cprintf("  -->%08x-%08x\n",PTE_ADDR(pde),PTE_ADDR(pde)+PTSIZE-1);
				va_cur+=PTSIZE;
				continue;
			}
			else{
				cprintf("\n");
				pte_t *pte = (pte_t *)(PTE_ADDR(pde)+KERNBASE);
				for(uint32_t i=0; i<1024 && va_cur <= PTE_ADDR(pde)+KERNBASE+PTSIZE-1 ; va_cur += PGSIZE, ++i){
					//cprintf("debug : va_cur: %08x\n",(uint32_t)va_cur);
					if(pte[i] &PTE_P){
						cprintf("  |--PTE");
						cprintf("(%03x) %08x-%08x %08x %c%c%c%c  -->%08x-%08x\n", PTX(va_cur),va_cur,va_cur+PGSIZE-1,PGSIZE,flag[pte[i]&PTE_P],flag[pte[i]&PTE_W],flag[pte[i]&PTE_U],flag[pte[i]&PTE_PS],PTE_ADDR(pte[i]),PTE_ADDR(pte[i])+PGSIZE-1);
					}
				}	
			}		
		}
		else{
			va_cur+=PTSIZE;
		}
		
		
	}
	return 0;
	
}




// Lab1 only
// read the pointer to the retaddr on the stack
static uint32_t
read_pretaddr() {
	uint32_t pretaddr;
    __asm __volatile("leal 4(%%ebp), %0" : "=r" (pretaddr)); 
    return pretaddr;
}

int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	// Your code here.
	cprintf("Stack backtrace:\n");
	cprintf("Stack backtrace:\n");
	uint32_t *ebp = (uint32_t *)read_ebp();
	while(ebp){
		cprintf("eip %08x ebp %08x args",*(ebp+1),ebp);
		for(int i=0; i<5; i++){
			cprintf(" %08x", *(ebp+i+2));
		}
		struct Eipdebuginfo info;
		debuginfo_eip(*(ebp+1),&info);
		cprintf("\n    %s:%d: %.*s+%d\n",info.eip_file,info.eip_line,info.eip_fn_namelen,info.eip_fn_name,*(ebp+1)-info.eip_fn_addr);
		ebp = (uint32_t *)(*ebp);
	}
    cprintf("Backtrace success\n");
	return 0;
}



/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int
runcmd(char *buf, struct Trapframe *tf)
{
	int argc;
	char *argv[MAXARGS];
	int i;

	// Parse the command buffer into whitespace-separated arguments
	argc = 0;
	argv[argc] = 0;
	while (1) {
		// gobble whitespace
		while (*buf && strchr(WHITESPACE, *buf))
			*buf++ = 0;
		if (*buf == 0)
			break;

		// save and scan past next arg
		if (argc == MAXARGS-1) {
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
	for (i = 0; i < NCOMMANDS; i++) {
		if (strcmp(argv[0], commands[i].name) == 0)
			return commands[i].func(argc, argv, tf);
	}
	cprintf("Unknown command '%s'\n", argv[0]);
	return 0;
}

void
monitor(struct Trapframe *tf)
{
	char *buf;

	cprintf("Welcome to the JOS kernel monitor!\n");
	cprintf("Type 'help' for a list of commands.\n");


	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}

// return EIP of caller.
// does not work if inlined.
// putting at the end of the file seems to prevent inlining.
unsigned
read_eip()
{
	uint32_t callerpc;
	__asm __volatile("movl 4(%%ebp), %0" : "=r" (callerpc));
	return callerpc;
}
