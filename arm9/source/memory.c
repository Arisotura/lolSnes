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

#include "cpu.h"
#include "memory.h"
#include "ppu.h"


u32 ROM_BaseOffset DTCM_BSS;
u32 ROM_HeaderOffset;
FILE* ROM_File DTCM_DATA = NULL;
u32 ROM_FileSize DTCM_BSS;

u8* ROM_Bank0;
u8* ROM_Bank0End;

u8 ROM_Region;

bool Mem_HiROM;
u8 Mem_SysRAM[0x20000];
u32 Mem_SRAMMask;
u8* Mem_SRAM = NULL;
FILE* Mem_SRAMFile = NULL;

char Mem_ROMPath[256] DTCM_BSS;
char Mem_SRAMPath[256] DTCM_BSS;

// addressing: BBBBBBBB:AAAaaaaa:aaaaaaaa
// bit0-27: argument
// bit28: access speed (0 = 6 cycles, 1 = 8 cycles)
// bit29: special bit (0 = argument is a RAM pointer, 1 = other case)
// bit30: write permission (0 = can write, 1 = read-only)
// common cases:
// * b29=0, b30=0: system RAM, SRAM; arg = pointer to RAM
// * b29=1, b30=0: I/O, expansion RAM; arg = zero
// * b29=0, b30=1: cached ROM; arg = pointer to RAM
// * b29=1, b30=1: non-cached ROM; arg = file offset
//
// cheat: we place stuff before the start of the actual array-- those 
// can be accessed quickly by the CPU core since it keeps a pointer to
// this table in one of the CPU registers
//
// table[-1] -> SRAM dirty flag
// table[-2] -> HBlank/VBlank flags
u32 _Mem_PtrTable[(MEMSTATUS_SIZE >> 2) + 0x800];
u32* Mem_PtrTable DTCM_BSS;
Mem_StatusData* Mem_Status;

IPCStruct* IPC;

u8 Mem_HVBJOY = 0x00;
u16 Mem_VMatch = 0;
u16 Mem_HMatchRaw = 0, Mem_HMatch = 0;
u16 Mem_HCheck = 0;

u8 Mem_MulA = 0;
u16 Mem_MulRes = 0;
u16 Mem_DivA = 0;
u16 Mem_DivRes = 0;

bool Mem_FastROM = false;

extern u8 DMA_HDMAFlag;



bool ROM_CheckHeader(u32 offset)
{
	if ((offset + 0x20) >= ROM_FileSize)
		return false;

	fseek(ROM_File, offset + 0x1C, SEEK_SET);
	
	u16 chksum, chkcomp;
	fread(&chkcomp, 2, 1, ROM_File);
	fread(&chksum, 2, 1, ROM_File);
	
	return (chkcomp ^ chksum) == 0xFFFF;
}


void reportBRK(u32 pc)
{
	iprintf("BRK @ %02X:%04X | %08X\n", pc>>16, pc&0xFFFF, MEM_PTR(pc>>16, pc&0xFFFF));
	for(;;);
}


bool Mem_LoadROM(char* path)
{
	if (ROM_File != NULL)
		fclose(ROM_File);
		
	strncpy(Mem_ROMPath, path, 256);
	
	ROM_File = fopen(path, "rb");
	if (!ROM_File) return false;

	fseek(ROM_File, 0, SEEK_END);
	ROM_FileSize = ftell(ROM_File);
	
	if (ROM_CheckHeader(0x81C0) || ROM_FileSize == 0x8200) // headered, LoROM
	{
		ROM_BaseOffset = 0x200;
		Mem_HiROM = false;
		ROM_HeaderOffset = 0x81C0;
		iprintf("ROM type: headered LoROM\n");
	}
	else if (ROM_CheckHeader(0x101C0)) // headered, HiROM
	{
		ROM_BaseOffset = 0x200;
		Mem_HiROM = true;
		ROM_HeaderOffset = 0x101C0;
		iprintf("ROM type: headered HiROM\n");
	}
	else if (ROM_CheckHeader(0x7FC0) || ROM_FileSize == 0x8000) // headerless, LoROM
	{
		ROM_BaseOffset = 0;
		Mem_HiROM = false;
		ROM_HeaderOffset = 0x7FC0;
		iprintf("ROM type: headerless LoROM\n");
	}
	else if (ROM_CheckHeader(0xFFC0)) // headerless, HiROM
	{
		ROM_BaseOffset = 0;
		Mem_HiROM = true;
		ROM_HeaderOffset = 0xFFC0;
		iprintf("ROM type: headerless HiROM\n");
	}
	else // whatever piece of shit
	{
		/*fclose(ROM_File);
		ROM_File = oldfile;
		return false;*/
		// assume header at 0x81C0
		// TODO use 0x7FC0 instead if no header
		ROM_BaseOffset = 0x200;
		Mem_HiROM = false;
		ROM_HeaderOffset = 0x81C0;
		iprintf("ROM type: not found, assuming headered LoROM\n");
	}

	fseek(ROM_File, ROM_HeaderOffset + 0x18, SEEK_SET);
	u8 sramsize; fread(&sramsize, 1, 1, ROM_File);
	u8 region; fread(&region, 1, 1, ROM_File);
	
	if (region <= 0x01 || (region >= 0x0D && region <= 0x10))
		ROM_Region = 0;
	else
		ROM_Region = 1;
	
	//
	
	//SPC_CycleRatio = ROM_Region ? 132990 : 134013;
	
	Mem_SRAMMask = sramsize ? ((1024 << sramsize) - 1) : 0;
	Mem_SRAMMask &= 0x000FFFFF;
	iprintf("SRAM size: %dKB\n", (Mem_SRAMMask+1) >> 10);
	
	if (Mem_SRAMMask)
	{
		strncpy(Mem_SRAMPath, path, strlen(path)-3);
		strncpy(Mem_SRAMPath + strlen(path)-3, "srm", 3);
		Mem_SRAMPath[strlen(path)] = '\0';
		FILE* sram = fopen(Mem_SRAMPath, "r+");
		if (!sram) sram = fopen(Mem_SRAMPath, "w+");
		if (sram) fclose(sram);
	}
	
	return true;
}

void Mem_Reset()
{
	u32 i, a, b;

	for (i = 0; i < (128 * 1024); i += 4)
		*(u32*)&Mem_SysRAM[i] = 0x55555555; // idk about this
		
	Mem_FastROM = false;
	
	DMA_HDMAFlag = 0;

	if (Mem_SRAM) 
	{
		free(Mem_SRAM);
		Mem_SRAM = NULL;
	}
	if (Mem_SRAMMask)
	{
		Mem_SRAM = malloc(Mem_SRAMMask + 1);
		for (i = 0; i <= Mem_SRAMMask; i += 4)
			*(u32*)&Mem_SRAM[i] = 0;
		
		Mem_SRAMFile = fopen(Mem_SRAMPath, "r");
		if (Mem_SRAMFile)
		{
			fread(Mem_SRAM, Mem_SRAMMask+1, 1, Mem_SRAMFile);
			fclose(Mem_SRAMFile);
			Mem_SRAMFile = NULL;
		}
	}
		
	Mem_Status = &_Mem_PtrTable[0];
	Mem_PtrTable = &_Mem_PtrTable[MEMSTATUS_SIZE >> 2];
	
	Mem_Status->SRAMDirty = 0;
	Mem_Status->HVBFlags = 0x00;
	Mem_Status->SRAMMask = Mem_SRAMMask;
	Mem_Status->IRQCond = 0;
	
	Mem_Status->VCount = 0;
	Mem_Status->HCount = 0;
	Mem_Status->IRQ_VMatch = 0;
	Mem_Status->IRQ_HMatch = 0;
	Mem_Status->IRQ_CurHMatch = 0x8000;
	
	Mem_Status->SPC_LastCycle = 0;
	
	Mem_Status->TotalLines = (ROM_Region ? 312 : 262) >> 1;
	Mem_Status->ScreenHeight = 224;
	
	//Mem_Status->SPC_CycleRatio = ROM_Region ? 0x000C51D9 : 0x000C39C6;
	//Mem_Status->SPC_CycleRatio += 0x1000; // hax -- TODO investigate why we need this to run at a somewhat proper rate
	Mem_Status->SPC_CycleRatio = 6400;//6418;//6400;//ROM_Region ? 132990 : 134013;
	Mem_Status->SPC_CyclesPerLine = Mem_Status->SPC_CycleRatio * 1364;
	//Mem_Status->SPC_CyclesPerLine = ROM_Region ? 0x41A41A42 : 0x4123D3B5;
	
	for (b = 0; b < 0x40; b++)
	{
		MEM_PTR(b, 0x0000) = MEM_PTR(0x80 + b, 0x0000) = MPTR_SLOW | (u32)&Mem_SysRAM[0];
		MEM_PTR(b, 0x2000) = MEM_PTR(0x80 + b, 0x2000) = MPTR_SPECIAL;
		MEM_PTR(b, 0x4000) = MEM_PTR(0x80 + b, 0x4000) = MPTR_SPECIAL;
		
		if ((b >= 0x30) && Mem_HiROM && Mem_SRAMMask)
			MEM_PTR(b, 0x6000) = MEM_PTR(0x80 + b, 0x6000) = MPTR_SLOW | MPTR_SRAM | (u32)&Mem_SRAM[(b << 13) & Mem_SRAMMask];
		else
			MEM_PTR(b, 0x6000) = MEM_PTR(0x80 + b, 0x6000) = MPTR_SLOW | MPTR_SPECIAL;
	}

	for (b = 1; b < 0x40; b++)
	{
		u32 offset = Mem_HiROM ? (0x8000 + (b << 16)) : (b << 15);
		
		for (a = 0; a < 0x8000; a += 0x2000)
			MEM_PTR(b, 0x8000 + a) = MEM_PTR(0x80 + b, 0x8000 + a) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + offset + a);
	}

	if (Mem_HiROM)
	{
		for (b = 0; b < 0x3E; b++)
			for (a = 0; a < 0x10000; a += 0x2000)
				MEM_PTR(0x40 + b, a) = MEM_PTR(0xC0 + b, a) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + (b << 16) + a);

		for (b = 0; b < 0x02; b++)
			for (a = 0; a < 0x10000; a += 0x2000)
				MEM_PTR(0x7E + b, a) = MPTR_SLOW | (u32)&Mem_SysRAM[(b << 16) + a];

		for (b = 0; b < 0x02; b++)
			for (a = 0; a < 0x10000; a += 0x2000)
				MEM_PTR(0xFE + b, a) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + 0x3E0000 + (b << 16) + a);
	}
	else
	{
		for (b = 0; b < 0x30; b++)
			for (a = 0; a < 0x10000; a += 0x2000)
				MEM_PTR(0x40 + b, a) = MEM_PTR(0xC0 + b, a) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + 0x200000 + (b << 15) + (a & 0x7FFF));

		if (Mem_SRAMMask)
		{
			for (b = 0; b < 0x0E; b++)
				for (a = 0; a < 0x8000; a += 0x2000)
					MEM_PTR(0x70 + b, a) = MEM_PTR(0xF0 + b, a) = MPTR_SLOW | MPTR_SRAM | (u32)&Mem_SRAM[((b << 15) + a) & Mem_SRAMMask];
			for (a = 0; a < 0x8000; a += 0x2000)
			{
				MEM_PTR(0xFE, a) = MPTR_SLOW | MPTR_SRAM | (u32)&Mem_SRAM[((0xE << 15) + a) & Mem_SRAMMask];
				MEM_PTR(0xFF, a) = MPTR_SLOW | MPTR_SRAM | (u32)&Mem_SRAM[((0xF << 15) + a) & Mem_SRAMMask];
			}
		}
		else
		{
			for (b = 0; b < 0x0E; b++)
				for (a = 0; a < 0x8000; a += 0x2000)
					MEM_PTR(0x70 + b, a) = MEM_PTR(0xF0 + b, a) = MPTR_SLOW | MPTR_SPECIAL;
			for (a = 0; a < 0x8000; a += 0x2000)
			{
				MEM_PTR(0xFE, a) = MPTR_SLOW | MPTR_SPECIAL;
				MEM_PTR(0xFF, a) = MPTR_SLOW | MPTR_SPECIAL;
			}
		}

		for (b = 0; b < 0x02; b++)
			for (a = 0; a < 0x10000; a += 0x2000)
				MEM_PTR(0x7E + b, a) = MEM_PTR(0xFE + b, a) = MPTR_SLOW | (u32)&Mem_SysRAM[(b << 16) + a];
	}
	
	ROM_SetupCache();
	
	iprintf("sysram = %08X\n", &Mem_SysRAM[0]);
	
	// get uncached address
	u32 ipcsize = (sizeof(IPCStruct) + 0x1F) & ~0x1F;
	IPC = memalign(32, ipcsize);u32 dorp = (u32)IPC;
	memset(IPC, 0, ipcsize);
	DC_FlushRange(IPC, ipcsize);
	IPC = memUncached(IPC);
	IPC->Pause = 0;
	iprintf("IPC struct = %08X\n", IPC);
	fifoSendValue32(FIFO_USER_01, 3);
	fifoSendAddress(FIFO_USER_01, memCached(IPC));
	iprintf("cached=%08X/%08X\n", memCached(IPC), dorp);
	iprintf("dorp=%08X\n", *(vu32*)0x04004008);
	Mem_HVBJOY = 0x00;
	
	Mem_MulA = 0;
	Mem_MulRes = 0;
	Mem_DivA = 0;
	Mem_DivRes = 0;
	
	PPU_Reset();
}


void Mem_SaveSRAM()
{
	if (!Mem_SRAMMask) 
		return;
	
	if (!Mem_Status->SRAMDirty)
		return;
	
	Mem_SRAMFile = fopen(Mem_SRAMPath, "r+");
	if (Mem_SRAMFile)
	{
		iprintf("SRAM save\n");
		Mem_Status->SRAMDirty = 0;
		fseek(Mem_SRAMFile, 0, SEEK_SET);
		fwrite(Mem_SRAM, Mem_SRAMMask+1, 1, Mem_SRAMFile);
		fclose(Mem_SRAMFile);
		Mem_SRAMFile = NULL;
	}
}


ITCM_CODE void report_unk_lol(u32 op, u32 pc)
{
	if (op == 0xDB) 
	{
		printf("STOP %06X\n", pc);
		return; 
	}

	printf("OP_UNK %08X %02X\n", pc, op);
	for (;;) swiWaitForVBlank();
}


inline u8 IO_ReadKeysLow()
{
	u16 keys = *(u16*)0x04000130;
	u8 keys2 = IPC->Input_XY;
	u8 ret = 0;
	
	if (!(keys & 0x0001)) ret |= 0x80;
	if (!(keys2 & 0x01)) ret |= 0x40;
	if (!(keys & 0x0200)) ret |= 0x20;
	if (!(keys & 0x0100)) ret |= 0x10;
	
	return ret;
}

inline u8 IO_ReadKeysHigh()
{
	u16 keys = *(u16*)0x04000130;
	u8 keys2 = IPC->Input_XY;
	u8 ret = 0;
	
	if (!(keys & 0x0002)) ret |= 0x80;
	if (!(keys2 & 0x02)) ret |= 0x40;
	if (!(keys & 0x0004)) ret |= 0x20;
	if (!(keys & 0x0008)) ret |= 0x10;
	if (!(keys & 0x0040)) ret |= 0x08;
	if (!(keys & 0x0080)) ret |= 0x04;
	if (!(keys & 0x0020)) ret |= 0x02;
	if (!(keys & 0x0010)) ret |= 0x01;
	
	return ret;
}


void SNES_RescheduleIRQ(u8 val)
{
	switch (val & 0x30)
	{
		case 0x00: Mem_Status->IRQ_CurHMatch = 0x8000; break;
		case 0x10: 
			Mem_Status->IRQ_CurHMatch = (Mem_Status->HCount > Mem_Status->IRQ_HMatch) ? 0x8000:Mem_Status->IRQ_HMatch; 
			break;
		case 0x20:
			Mem_Status->IRQ_CurHMatch = (Mem_Status->VCount != Mem_Status->IRQ_VMatch) ? 0x8000:0; 
			break;
		case 0x30:
			Mem_Status->IRQ_CurHMatch = 
				((Mem_Status->VCount != Mem_Status->IRQ_VMatch) || 
				 (Mem_Status->HCount > Mem_Status->IRQ_HMatch))
				 ? 0x8000:Mem_Status->IRQ_HMatch; 
			break;
	}
}


u8 Mem_GIORead8(u32 addr)
{
	u8 ret = 0;
	switch (addr)
	{
		case 0x10:
			if (Mem_Status->HVBFlags & 0x20)
			{
				ret = 0x80;
				Mem_Status->HVBFlags &= 0xDF;
			}
			break;
			
		case 0x11:
			if (Mem_Status->HVBFlags & 0x10)
			{
				ret = 0x80;
				Mem_Status->HVBFlags &= 0xEF;
			}
			break;
			
		case 0x12:
			ret = Mem_Status->HVBFlags & 0x80;
			if (Mem_Status->HCount >= 1024) ret |= 0x40;
			break;
			
		case 0x14:
			ret = Mem_DivRes & 0xFF;
			break;
		case 0x15:
			ret = Mem_DivRes >> 8;
			break;
			
		case 0x16:
			ret = Mem_MulRes & 0xFF;
			break;
		case 0x17:
			ret = Mem_MulRes >> 8;
			break;
			
		case 0x18:
			ret = IO_ReadKeysLow();
			break;
		case 0x19:
			ret = IO_ReadKeysHigh();
			break;
	}

	return ret;
}

u16 Mem_GIORead16(u32 addr)
{
	u16 ret = 0;
	switch (addr)
	{
		case 0x14:
			ret = Mem_DivRes;
			break;
			
		case 0x16:
			ret = Mem_MulRes;
			break;
			
		case 0x18:
			ret = IO_ReadKeysLow() | (IO_ReadKeysHigh() << 8);
			break;
			
		default:
			ret = Mem_GIORead8(addr);
			ret |= (Mem_GIORead8(addr + 1) << 8);
			break;
	}

	return ret;
}

void Mem_GIOWrite8(u32 addr, u8 val)
{
	switch (addr)
	{
		case 0x00:
			if ((Mem_Status->IRQCond ^ val) & 0x30) // reschedule the IRQ if needed
				SNES_RescheduleIRQ(val);
			if (!(val & 0x30)) // acknowledge current IRQ if needed
				Mem_Status->HVBFlags &= 0xEF;
			Mem_Status->IRQCond = val;
			break;
			
		case 0x02:
			Mem_MulA = val;
			break;
		case 0x03:
			Mem_MulRes = (u16)Mem_MulA * (u16)val;
			Mem_DivRes = (u16)val;
			break;
			
		case 0x04:
			Mem_DivA = (Mem_DivA & 0xFF00) | val;
			break;
		case 0x05:
			Mem_DivA = (Mem_DivA & 0x00FF) | (val << 8);
			break;
		case 0x06:
			*(u16*)0x04000280 = 0;
			*(u32*)0x04000290 = (u32)Mem_DivA;
			*(u32*)0x04000298 = (u32)val;
			for (;;)
			{
				u16 divcnt = *(volatile u16*)0x04000280;
				if (!(divcnt & 0x8000)) break;
			}
			Mem_DivRes = *(volatile u16*)0x040002A0;
			Mem_MulRes = *(volatile u16*)0x040002A8;
			break;
			
		case 0x07:
			Mem_Status->IRQ_HMatch &= 0x0400;
			Mem_Status->IRQ_HMatch |= (val << 2);
			if (Mem_Status->IRQCond & 0x10) SNES_RescheduleIRQ(Mem_Status->IRQCond);
			break;
		case 0x08:
			Mem_Status->IRQ_HMatch &= 0x03FC;
			Mem_Status->IRQ_HMatch |= ((val & 0x01) << 10);
			if (Mem_Status->IRQCond & 0x10) SNES_RescheduleIRQ(Mem_Status->IRQCond);
			break;
			
		case 0x09:
			Mem_Status->IRQ_VMatch &= 0x0100;
			Mem_Status->IRQ_VMatch |= val;
			if (Mem_Status->IRQCond & 0x20) SNES_RescheduleIRQ(Mem_Status->IRQCond);
			break;
		case 0x0A:
			Mem_Status->IRQ_VMatch &= 0x00FF;
			Mem_Status->IRQ_VMatch |= ((val & 0x01) << 8);
			if (Mem_Status->IRQCond & 0x20) SNES_RescheduleIRQ(Mem_Status->IRQCond);
			break;
			
		case 0x0B:
			DMA_Enable(val);
			break;
		case 0x0C:
			DMA_HDMAFlag = val;
			break;
			
		case 0x0D:
			{
				bool fast = (val & 0x01);
				if (fast ^ Mem_FastROM)
				{
					Mem_FastROM = fast;
					ROM_SpeedChanged();
				}
			}
			break;
	}
}

void Mem_GIOWrite16(u32 addr, u16 val)
{
	switch (addr)
	{
		case 0x02:
			Mem_MulA = val & 0xFF;
			Mem_MulRes = (u16)Mem_MulA * (val >> 8);
			Mem_DivRes = (u16)val;
			break;
			
		case 0x04:
			Mem_DivA = val;
			break;
			
		case 0x07:
			Mem_Status->IRQ_HMatch = (val & 0x01FF) << 2;
			if (Mem_Status->IRQCond & 0x10) SNES_RescheduleIRQ(Mem_Status->IRQCond);
			break;
			
		case 0x09:
			Mem_Status->IRQ_VMatch = val & 0x01FF;
			if (Mem_Status->IRQCond & 0x20) SNES_RescheduleIRQ(Mem_Status->IRQCond);
			break;
			
		case 0x0B:
			DMA_Enable(val & 0xFF);
			DMA_HDMAFlag = val >> 8;
			break;
			
		default:
			Mem_GIOWrite8(addr, val & 0xFF);
			Mem_GIOWrite8(addr + 1, val >> 8);
			break;
	}
}


u8 Mem_JoyRead8(u32 addr)
{
	u8 ret = 0;

	// this isn't proper or even nice
	// games that actually require manual joypad I/O will fuck up
	// but this seems to convince SMAS that there is a joystick plugged in
	if (addr == 0x16) ret = 0x01;
	
	return ret;
}

u16 Mem_JoyRead16(u32 addr)
{
	u16 ret = 0;
	
	//iprintf("joy read16 40%02X\n", addr);

	return ret;
}

void Mem_JoyWrite8(u32 addr, u8 val)
{
}

void Mem_JoyWrite16(u32 addr, u16 val)
{
}


u8 Mem_Read8(u32 addr)
{
	u32 ptr = Mem_PtrTable[addr >> 13];
	if (ptr & MPTR_SPECIAL)
	{
		if (ptr & MPTR_READONLY)
		{
			ptr &= 0x0FFFFFFF;
			ptr += (addr & 0x1FFF);
			return Mem_ROMRead8(ptr);
		}
		else
			return Mem_IORead8(addr);
	}
	else
	{
		u8* mptr = (u8*)(ptr & 0x0FFFFFFF);
		return mptr[addr & 0x1FFF];
	}
}

u16 Mem_Read16(u32 addr)
{
	u32 ptr = Mem_PtrTable[addr >> 13];
	if (ptr & MPTR_SPECIAL)
	{
		if (ptr & MPTR_READONLY)
		{
			ptr &= 0x0FFFFFFF;
			ptr += (addr & 0x1FFF);
			return Mem_ROMRead16(ptr);
		}
		else
			return Mem_IORead16(addr);
	}
	else
	{
		u8* mptr = (u8*)(ptr & 0x0FFFFFFF);
		addr &= 0x1FFF;
		return mptr[addr] | (mptr[addr + 1] << 8);
	}
}

void Mem_Write8(u32 addr, u8 val)
{
	u32 ptr = Mem_PtrTable[addr >> 13];
	if (ptr & MPTR_READONLY) return;
	if (ptr & MPTR_SPECIAL)
		Mem_IOWrite8(addr, val);
	else
	{
		u8* mptr = (u8*)(ptr & 0x0FFFFFFF);
		mptr[addr & 0x1FFF] = val;
	}
}

void Mem_Write16(u32 addr, u8 val)
{
	u32 ptr = Mem_PtrTable[addr >> 13];
	if (ptr & MPTR_READONLY) return;
	if (ptr & MPTR_SPECIAL)
		Mem_IOWrite16(addr, val);
	else
	{
		u8* mptr = (u8*)(ptr & 0x0FFFFFFF);
		addr &= 0x1FFF;
		mptr[addr] = val & 0xFF;
		mptr[addr + 1] = val >> 8;
	}
}
