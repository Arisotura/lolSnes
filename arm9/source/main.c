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
	iprintf("printvar %02X%02X\n", SPC_IOPorts[9], SPC_IOPorts[8]);
	asm("ldmia sp!, {r12}");
}
