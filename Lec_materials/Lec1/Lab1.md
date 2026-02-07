# Lab1 Configuration - Fall 2018

## Basic Concepts

**Real Mode:**
Real mode is a simple operating mode, designed to support the first PCs, such as the Intel 8088. In this mode, memory addresses are directly mapped—the virtual address is equal to the physical address.


## Toolchain Preparation

* QEMU emulator (correct version required)
* GDB tools


## Exercises

### PC Bootstrap

* **Exercise 1:** Use the JOS system. JOS is a 16-bit system with x86-i386 architecture. 16-bit PCs are only capable of addressing 1MB of physical memory, using an offset to calculate the physical address from the virtual address.

* **Exercise 2:** GDB usage is the main focus.

### Bootloader

* **Exercise 3:** Use GDB to debug the boot loader. Set a breakpoint at `0x7c00` and answer the following questions:

    * **At what point does the processor start executing 32-bit code? What exactly causes the switch from 16-bit to 32-bit mode?**

        After the "Enable A20" stage, the system switches from real mode to protected mode using a bootstrap GDT and segment translation that makes virtual addresses identical to physical addresses, so that the effective memory map does not change during the switch. The point where 32-bit code execution begins is at address `0x7c2d`, where we find the `ljmp` instruction. In 16-bit mode, 720 bytes are executed.

    * **What is the last instruction of the boot loader executed, and what is the first instruction of the kernel it just loaded?**

        The boot loader executes `((void (*)(void)) (ELFHDR->e_entry))();` at `0x7d71`, which corresponds to the assembly instruction `call *0x10018`. Between the boot loader executable and the kernel file, there are additional register setup commands. After those, the kernel's first instruction is in `entry.S:74` (relocated): it clears the frame pointer register (EBP) so that stack backtraces will be terminated properly once we start debugging C code.

    * **Where is the first instruction of the kernel?**

        It is at `0xf010002f`.

    * **How does the boot loader decide how many sectors it must read in order to fetch the entire kernel from disk? Where does it find this information?**

        The boot loader reads the information from the ELF header and uses the offset to incrementally read sectors. This information is stored in the ELF header.


> **Boot Loader Execution Order:**
> Machine boot → Enter 16-bit real mode → Enable A20 (enabling extended addressing) → Enter 32-bit protected mode → Call `bootmain()` → Read bytes from disk to RAM (segment count from ELF header) → Call kernel entry → Boot loader finished.

* **Exercise 4:** C pointer exercises.

* **Exercise 5:** In `jos/boot/Makefrag`, the boot loader loading address is set to `0x7c00`. This address is chosen because it allows disabling interrupts so that the system will not keep restarting.

* **Exercise 6:** When in the bootloader, `x /8x 0x100000` always shows 0. After calling `entry()` in `main.c`, it begins to have values. The `entry()` function builds the mapping from virtual addresses to physical addresses.

### Kernel

* **Exercise 7:** Kernel analysis:

    * From `objdump`: The bootloader's load address and link address are the same, but they differ in the kernel—loaded at `0x00100000` and linked at `0xf0100000`.

    * In `entry()` (at `0x10000c`): Before entering the kernel code, execution first goes through `entry.S`. Based on the information above, the kernel code is linked and runs from `0xf0100000`, so we need to map virtual memory to physical memory within the 4MB memory space in Lab1. The mapping is accomplished by the page table, which is hand-written in `entrypgdir.c`, mapping 2 virtual memory (VM) ranges to physical memory (PM). After `entry.S` sets the `CR0_PG` flag, the virtual memory mapping is complete. Then the code jumps to `KERNBASE` and calls `i386_init()`.

    * If we comment out the important code `movl %eax, %cr0`: The system exits because it cannot find address `0xf0100000` and gets stuck in `entry.S`. The `x /x` command outputs addresses not under `0xf0400000`.

* **Exercise 8:** `printf()` implementation:

    * `print.c` mainly implements the basic print function. `printfmt.c` mainly implements output formatting based on the string content. `console.c` mainly implements the keyboard input function and initializes some serial devices.

> **Exercise Code:** Finished implementing `%o` format output.
>
> **Original code:**
```c
        // (unsigned) octal
		case 'o':
			// Replace this with your code.
			putch('X', putdat);
			putch('X', putdat);
			putch('X', putdat);
			break;
```

> **Modified code:**
```c
		// (unsigned) octal
		case 'o':
			num = getint(&ap, lflag);
			putch('0',putdat);
			base = 8;

			goto number;
			break;
```

> **Answering the Questions:**

* ***Explain the interface between printf.c and console.c. Specifically, what function does console.c export? How is this function used by printf.c?***

    `console.c` exports `cons_putc()` for `printf.c`, which supports outputting individual characters to the console.

* ***Explain the following from console.c:***

```c
1      if (crt_pos >= CRT_SIZE) {
2              int i;
3              memmove(crt_buf, crt_buf + CRT_COLS, (CRT_SIZE - CRT_COLS) * sizeof(uint16_t));
4              for (i = CRT_SIZE - CRT_COLS; i < CRT_SIZE; i++)
5                      crt_buf[i] = 0x0700 | ' ';
6              crt_pos -= CRT_COLS;
7      }
```

`crt_pos` is the position pointer. The console size is 25×80 characters. If the pointer moves past the edge, the content scrolls up by one line: all existing data moves up, the bottom line is cleared, and the pointer moves back by one row.

* ***Let's say that GCC changed its calling convention so that it pushed arguments on the stack in declaration order, so that the last argument is pushed last. How would you have to change cprintf or its interface so that it would still be possible to pass it a variable number of arguments?***

> **Challenge:**
> Enhance the console to allow text to be printed in different colors. The traditional way to do this is to make it interpret ANSI escape sequences embedded in the text strings printed to the console, but you may use any mechanism you like. There is plenty of information on the 6.828 reference page and elsewhere on the web on programming the VGA display hardware. If you're feeling really adventurous, you could try switching the VGA hardware into a graphics mode and making the console draw text onto the graphical frame buffer.

#### Stack

* **Exercise 9:** The kernel initializes the stack in `entry.S` with `movl $(bootstacktop), %esp` (at `0xf0100034`). The kernel pushes the `entry_pgdir` memory address into `%esp` and pushes `0x00` into `%ebp` to reserve the kernel space. The `%esp` points to `0xf010b021`, and the kernel base is `0xf0000000`. This is also the stack top position.

* **Exercise 10:** Stack Backtrace: Each time `test_backtrace` is called, the stack-frame's `%eip` register (also called the Program Counter register) stores the return address, which points to the next instruction after the call. Calling another `test_backtrace` enters a new stack frame. At the beginning of the new frame, `push %ebp` and `mov %esp, %ebp` store the stack-frame information in `%ebp`. Each call to `test_backtrace` pushes 2 32-bit words onto the stack: `%ebp` and `%ebx`, storing stack information and arguments.

* **Exercise 11:** Implement the function `mon_backtrace()` in `monitor.c`.

    Code reference:
```c
	// Your code here.

	uint32_t *ebp;

	ebp = (uint32_t *) read_ebp();
	cprintf("Stack Backtrace:\n");

	while (ebp!=0) {
		cprintf("  ebp %08x",ebp);
		cprintf("  eip %08x",*(ebp+1));

		cprintf(" args");
		cprintf(" %08x",*(ebp+2));
		cprintf(" %08x",*(ebp+3));
		cprintf(" %08x",*(ebp+4));
		cprintf(" %08x",*(ebp+5));
		cprintf(" %08x\n",*(ebp+6));

		ebp = (uint32_t*) *ebp;
	}
```

* **Exercise 12:** Modify your stack backtrace function to display, for each `eip`, the function name, source file name, and line number corresponding to that `eip`.

    * Refer to the lab's information to understand where `__STAB_*` comes from.

```c
	// Search within [lline, rline] for the line number stab.
	// If found, set info->eip_line to the right line number.
	// If not found, return -1.
	//
	// Hint:
	//	There's a particular stabs type used for line numbers.
	//	Look at the STABS documentation and <inc/stab.h> to find
	//	which one.
	// Your code here.
	stab_binsearch(stabs, &lline, &rline, N_SLINE, addr);

	if(lline <= rline){
		info->eip_line = stabs[rline].n_desc;
	}else{
		return -1;
	}
```
