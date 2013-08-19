#include <nds.h>
#include <stdio.h>
#ifdef NITROFS_ROM
#include <filesystem.h>
#else
#include <fat.h>
#endif

#include "cpu.h"
#include "memory.h"
#include "ppu.h"

// debug
u32 lolpc = 0;

ITCM_CODE void vblank()
{
	PPU_VBlank();
	
	//u16 keys = *(volatile u16*)0x04000130;
	//if (!(keys & 0x0001)) iprintf("SPC PC = %04X\nCPU PC = %04X\n", IPC->_debug, lolpc >> 16);
}


int main(void)
{
	defaultExceptionHandler();
	
	irqEnable(IRQ_VBLANK);
	irqSet(IRQ_VBLANK, vblank);
	
	//vramSetBankA(VRAM_A_LCD);
	videoSetMode(MODE_0_2D);

	//consoleDemoInit();
	*(u8*)0x04000242 = 0x82;
	*(u8*)0x04000248 = 0x81;
	videoSetModeSub(MODE_0_2D);
	consoleInit(NULL, 0, BgType_Text4bpp, BgSize_T_256x256, 2, 0, false, true);
	
#ifdef NITROFS_ROM
	if (!nitroFSInit())
#else
	if (!fatInitDefault())
#endif
	{
		iprintf("FAT init failed\n");
		return -1;
	}

	iprintf("lolSnes v1.0 -- by Mega-Mario\n");
	
	if (!Mem_LoadROM("snes/rom.smc"))
	{
		iprintf("ROM loading failed\n");
		return -1;
	}
	
	iprintf("ROM loaded, running\n");

	CPU_Reset();
	fifoSendValue32(FIFO_USER_01, 1);
	
	swiWaitForVBlank();
	fifoSendValue32(FIFO_USER_01, 2);

	swiWaitForVBlank();
	CPU_Run();

	return 0;
}

void printvar()
{
	asm("stmdb sp!, {r12}");
	iprintf("printvar %04X\n", IPC->_debug);
	asm("ldmia sp!, {r12}");
}

void printstuff(u32 foo, u32 bar, u32 blarg)
{
	asm("stmdb sp!, {r0-r3, r12}");
	iprintf("printstuff %08X %08X %08X\n", foo, bar, blarg);
	asm("ldmia sp!, {r0-r3, r12}");
}
