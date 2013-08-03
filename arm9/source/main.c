#include <nds.h>
#include <stdio.h>
#ifdef NITROFS_ROM
#include <filesystem.h>
#else
#include <fat.h>
#endif

#include "cpu.h"
#include "memory.h"


int main(void)
{
	defaultExceptionHandler();
	
	//vramSetBankA(VRAM_A_LCD);
	videoSetMode(MODE_0_2D);

	consoleDemoInit();
	
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

	//CPU_Run();
	//iprintf("%08X\n", (u32)&CPU_Regs);
	//iprintf("%08X %08X %08X %04X%04X\n", CPU_Regs.nCycles, CPU_Regs.PC, CPU_Regs.P.val, CPU_Regs.DBR, CPU_Regs.D);
	//iprintf("%04X%04X %08X %08X %08X\n", CPU_Regs.PBR, CPU_Regs.S, CPU_Regs.Y, CPU_Regs.X, CPU_Regs.A);
	CPU_Reset();
	fifoSendValue32(FIFO_USER_01, 1);
	
	swiWaitForVBlank();
	fifoSendValue32(FIFO_USER_01, 2);

	swiWaitForVBlank();
	CPU_Run();
	/*for (;;)
	{
		CPU_Run();
		swiWaitForVBlank();
	}*/

	return 0;
}
