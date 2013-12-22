/*
    Copyright 2013 Mega-Mario

    This file is part of lolSnes.

    lolSnes is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    lolSnes is distributed in the hope that it will be useful, but WITHOUT ANY 
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS 
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along 
    with lolSnes. If not, see http://www.gnu.org/licenses/.
*/

#include <nds.h>
#include <stdio.h>
#include <dirent.h>
#ifdef NITROFS_ROM
#include <filesystem.h>
#else
#include <fat.h>
#endif

#include "cpu.h"
#include "memory.h"
#include "ppu.h"

#include "lolsnes_screen.h"


#define VERSION "v1.1"


void doSplashscreen()
{
	int i;
	
	u16* pal = (u16*)0x05000000;
	for (i = 0; i < (lolsnes_screenPalLen >> 1); i++)
		pal[i] = lolsnes_screenPal[i];
	
	videoBgEnable(0);
	
	int mapblock = ((lolsnes_screenTilesLen + 0x7FF) >> 11);
	*(vu16*)0x04000008 = (mapblock << 8) | 0x0080;
	
	u32* tiles = (u32*)0x06000000;
	for (i = 0; i < (lolsnes_screenTilesLen >> 2); i++)
		tiles[i] = lolsnes_screenTiles[i];
		
	u16* map = (u16*)(0x06000000 + (mapblock << 11));
	for (i = 0; i < (lolsnes_screenMapLen >> 1); i++)
		map[i] = lolsnes_screenMap[i];
		
	*(vu16*)0x04000010 = 0;
	*(vu16*)0x04000012 = 0;
}


bool running = false;


void arm7print(u32 value32, void* userdata)
{
	iprintf(IPC->Dbg_String);
}


void sleepMode(u32 value32, void* userdata)
{iprintf("sleep mode engaged\n");for(;;);
	fifoSendValue32(FIFO_USER_03, 1);
	
	// turn shit off
	u32 powerstate = *(vu32*)0x04000304;
	*(vu32*)0x04000304 = (powerstate & 0x8001);
	
	swiIntrWait(1, IRQ_FIFO_NOT_EMPTY);
	iprintf("done sleeping\n");
	// turn shit back on
	*(vu32*)0x04000304 = powerstate;
	
	fifoSendValue32(FIFO_USER_03, 1);
	while (!fifoCheckValue32(FIFO_USER_03));
}


char* filelist;
int nfiles;

bool isGoodFile(struct dirent* entry)
{
	if (entry->d_type != DT_REG) return false;
	
	char* ext = &entry->d_name[strlen(entry->d_name) - 4];
	if (strncmp(ext, ".smc", 4) && strncmp(ext, ".sfc", 4)) return false;
	
	return true;
}

void makeROMList()
{
	DIR* romdir = opendir("snes");
	int i = 0;
	if (romdir)
	{
		struct dirent* entry;
		
		while (entry = readdir(romdir))
		{
			if (!isGoodFile(entry)) continue;
			i++;
		}
			
		rewinddir(romdir);
		
		filelist = (char*)malloc(i * 256);
		nfiles = i;
		i = 0;
		while (entry = readdir(romdir))
		{
			if (!isGoodFile(entry)) continue;
			strncpy(&filelist[i << 8], entry->d_name, 255);
			filelist[(i << 8) + 255] = '\0';
			i++;
		}
		
		closedir(romdir);
	}
}


bool debug_on = false;

void toggleConsole(bool show)
{
	debug_on = show;
	
	if (show)
	{
		videoBgEnableSub(0);
		videoBgDisableSub(1);
	}
	else
	{
		videoBgEnableSub(1);
		videoBgDisableSub(0);
	}
}

u32 framecount = 0;
u16 lastkeys = 0x03FF;
u16 keypress = 0x03FF;

ITCM_CODE void vblank()
{
	PPU_VBlank();
	
	// every 8 frames, check if SRAM needs to be saved
	framecount++;
	if (!(framecount & 0x7)) Mem_SaveSRAM();
}

void vblank_idle()
{
	// debug: toggle menu/console when pressing L+R+Down+B
	u16 keys = *(vu16*)0x04000130;
	if (keys != lastkeys)
	{
		lastkeys = keys;
		keypress = keys;
		if (!(keys & 0x0382))
			toggleConsole(!debug_on);
	}
}


int menusel = 0;
int menuscroll = 0;

void setMenuSel(int sel)
{
	*(vu16*)0x04001040 = 0x00FE;
	*(vu16*)0x04001044 = (sel << 11) | ((sel+1) << 3);
}

void menuPrint(int x, int y, char* str)
{
	u16* menu = (u16*)0x06201800;
	int i;
	
	menu += (y << 5) + x;
	
	for (i = 0; str[i] != '\0'; i++)
	{
		menu[i] = 0xF000 | str[i];
	}
}

void makeMenu()
{
	menuPrint(0, 0, "- lolSnes " VERSION " - by Mega-Mario -");
	menuPrint(0, 1, "________________________________");
	
	int i;
	int maxfile;
	
	if ((nfiles - menuscroll) <= 22) maxfile = (nfiles - menuscroll);
	else maxfile = 22;
	
	for (i = 0; i < maxfile; i++)
	{
		menuPrint(0, 2+i, "                                ");
		menuPrint(2, 2+i, &filelist[(menuscroll+i) << 8]);
		if ((menuscroll+i) == menusel)
			menuPrint(0, 2+i, "\x10");
	}
	
	menuPrint(31, 2, "\x1E");
	menuPrint(31, 23, "\x1F");
	for (i = 3; i < 23; i++)
		menuPrint(31, i, "|");
		
	if ((nfiles - menuscroll) <= 22)
		menuPrint(31, 22, "\x08");
	else
		menuPrint(31, 3 + ((menuscroll * 20) / (nfiles - 22)), "\x08");
		
	setMenuSel(2 + menusel - menuscroll);
}


char fullpath[270];

int main(void)
{
	int i;
	
	defaultExceptionHandler();
	
	irqEnable(IRQ_VBLANK);
	irqEnable(IRQ_HBLANK);
	
	irqSet(IRQ_VBLANK, vblank_idle);
	
	fifoSetValue32Handler(FIFO_USER_02, arm7print, NULL);
	fifoSetValue32Handler(FIFO_USER_03, sleepMode, NULL);
	
	//vramSetBankA(VRAM_A_LCD);
	videoSetMode(MODE_0_2D);
	*(vu8*)0x04000240 = 0x81;
	doSplashscreen();

	// map some VRAM
	// bank C to ARM7, bank H for subscreen graphics
	*(vu8*)0x04000242 = 0x82;
	*(vu8*)0x04000248 = 0x81;
	
	videoSetModeSub(MODE_0_2D);
	consoleInit(NULL, 0, BgType_Text4bpp, BgSize_T_256x256, 2, 0, false, true);
	
	*(vu16*)0x0400100A = 0x0300;
	
	setBackdropColorSub(0x7C00);
	
	// configure BLDCNT so that backdrop becomes black
	*(vu16*)0x04001050 = 0x00E0;
	*(vu8*)0x04001054 = 16;
	
	// enable window 0 and disable color effects inside it
	*(vu16*)0x04001000 |= 0x2000;
	*(vu16*)0x04001048 = 0x001F;
	*(vu16*)0x0400104A = 0x003F;
	
	toggleConsole(false);
	
#ifdef NITROFS_ROM
	if (!nitroFSInit())
#else
	if (!fatInitDefault())
#endif
	{
		toggleConsole(true);
		iprintf("FAT init failed\n");
		return -1;
	}
	
	makeROMList();
	
	makeMenu();

	iprintf("lolSnes " VERSION " -- by Mega-Mario\n");
	iprintf("http://lolsnes.net/\n");
	
	for (;;)
	{
		if (keypress != 0x03FF)
		{
			if (!(keypress & 0x0040)) // up
			{
				menusel--;
				if (menusel < 0) menusel = 0;
				if (menusel < menuscroll) menuscroll = menusel;
				makeMenu();
			}
			else if (!(keypress & 0x0080)) // down
			{
				menusel++;
				if (menusel > nfiles-1) menusel = nfiles-1;
				if (menusel-21 > menuscroll) menuscroll = menusel-21;
				makeMenu();
			}
			else if ((keypress & 0x0003) != 0x0003) // A/B
			{
				strncpy(fullpath, "snes/", 5);
				strncpy(fullpath + 5, &filelist[menusel << 8], 256);
				
				if (!Mem_LoadROM(fullpath))
				{
					iprintf("ROM loading failed\n");
					continue;
				}
				
				*(vu16*)0x04001000 &= 0xDFFF;
				toggleConsole(true);
				iprintf("ROM loaded, running\n");

				CPU_Reset();
				fifoSendValue32(FIFO_USER_01, 1);
				
				swiWaitForVBlank();
				fifoSendValue32(FIFO_USER_01, 2);
				
				irqSet(IRQ_VBLANK, vblank);
				irqSet(IRQ_HBLANK, PPU_HBlank);

				swiWaitForVBlank();
				CPU_Run();
			}
			
			keypress = 0x03FF;
		}
		
		swiWaitForVBlank();
	}

	return 0;
}

void printvar()
{
	asm("stmdb sp!, {r12}");
	//iprintf("printvar %04X\n", IPC->_debug);
	asm("ldmia sp!, {r12}");
}

void printstuff(u32 foo, u32 bar, u32 blarg)
{
	asm("stmdb sp!, {r0-r3, r12}");
	iprintf("printstuff %08X %08X %08X\n", foo, bar, blarg);
	asm("ldmia sp!, {r0-r3, r12}");
}
