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
#include <fat.h>

#include "cpu.h"
#include "memory.h"
#include "ppu.h"


#define MEM_PTR(b, a) Mem_PtrTable[((b) << 3) | ((a) >> 13)]

#define MPTR_SLOW		(1 << 28)
#define MPTR_SPECIAL	(1 << 29)
#define MPTR_READONLY	(1 << 30)
#define MPTR_SRAM		(1 << 31)

u32 ROM_BaseOffset DTCM_BSS;
u32 ROM_HeaderOffset;
FILE* ROM_File DTCM_DATA = NULL;
u32 ROM_FileSize DTCM_BSS;
/*u32 ROM_CodeCacheBank DTCM_DATA = 0x100;
u8 ROM_CodeCache[0x10000];
u32 ROM_DataCacheBank DTCM_DATA = 0x100;
u8 ROM_DataCache[0x10000];*/
u8* ROM_Bank0;
u8* ROM_Bank0End;
u8* ROM_Cache[2 + ROMCACHE_SIZE] DTCM_BSS;
int ROM_CacheBank[2 + ROMCACHE_SIZE] DTCM_BSS;
u8 ROM_CacheIndex DTCM_BSS;
int ROM_CacheInited = 0;

//void (*ROM_CacheCode)(u32 bank) DTCM_BSS;
//void (*ROM_CacheData)(u32 bank) DTCM_BSS;

bool Mem_HiROM;
u8 Mem_SysRAM[0x20000];
u32 Mem_SRAMMask;
u8* Mem_SRAM = NULL;

char Mem_ROMPath[256] DTCM_BSS;
char Mem_SRAMPath[256] DTCM_BSS;

//u8 Mem_IO_21xx[0x100] DTCM_BSS;
//u8 Mem_IO_42xx[0x20] DTCM_BSS;
//u8 Mem_IO_43xx[0x80] DTCM_BSS;

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
u32 _Mem_PtrTable[0x1 + 0x800] DTCM_BSS;
u32* Mem_PtrTable DTCM_BSS;

IPCStruct* IPC;

u8 Mem_HVBJOY = 0x00;
u16 Mem_VMatch = 0;

u8 Mem_MulA = 0;
u16 Mem_MulRes = 0;
u16 Mem_DivA = 0;
u16 Mem_DivRes = 0;


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

void ROM_DoUncacheBank(int bank)
{
	bank &= 0x7F;
	if (Mem_HiROM && bank < 0x40) bank += 0x40;
	
	if (Mem_HiROM)
	{
		u32 b = (bank - 0x40) << 16;
		
		if (bank < 0x7E)
		{
			MEM_PTR(bank, 0x0000) = MEM_PTR(0x80 + bank, 0x0000) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + b + 0x0000);
			MEM_PTR(bank, 0x2000) = MEM_PTR(0x80 + bank, 0x2000) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + b + 0x2000);
			MEM_PTR(bank, 0x4000) = MEM_PTR(0x80 + bank, 0x4000) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + b + 0x4000);
			MEM_PTR(bank, 0x6000) = MEM_PTR(0x80 + bank, 0x6000) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + b + 0x6000);
			MEM_PTR(bank, 0x8000) = MEM_PTR(0x80 + bank, 0x8000) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + b + 0x8000);
			MEM_PTR(bank, 0xA000) = MEM_PTR(0x80 + bank, 0xA000) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + b + 0xA000);
			MEM_PTR(bank, 0xC000) = MEM_PTR(0x80 + bank, 0xC000) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + b + 0xC000);
			MEM_PTR(bank, 0xE000) = MEM_PTR(0x80 + bank, 0xE000) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + b + 0xE000);
		}
		else
		{
			MEM_PTR(0x80 + bank, 0x0000) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + b + 0x0000);
			MEM_PTR(0x80 + bank, 0x2000) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + b + 0x2000);
			MEM_PTR(0x80 + bank, 0x4000) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + b + 0x4000);
			MEM_PTR(0x80 + bank, 0x6000) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + b + 0x6000);
			MEM_PTR(0x80 + bank, 0x8000) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + b + 0x8000);
			MEM_PTR(0x80 + bank, 0xA000) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + b + 0xA000);
			MEM_PTR(0x80 + bank, 0xC000) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + b + 0xC000);
			MEM_PTR(0x80 + bank, 0xE000) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + b + 0xE000);
		}
		
		MEM_PTR(bank - 0x40, 0x8000) = MEM_PTR(0x40 + bank, 0x8000) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + b + 0x8000);
		MEM_PTR(bank - 0x40, 0xA000) = MEM_PTR(0x40 + bank, 0xA000) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + b + 0xA000);
		MEM_PTR(bank - 0x40, 0xC000) = MEM_PTR(0x40 + bank, 0xC000) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + b + 0xC000);
		MEM_PTR(bank - 0x40, 0xE000) = MEM_PTR(0x40 + bank, 0xE000) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + b + 0xE000);
	}
	else
	{
		if (bank < 0x7E)
		{
			if (bank >= 0x40 && bank < 0x70)
			{
				MEM_PTR(bank, 0x0000) = MEM_PTR(0x80 + bank, 0x0000) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + (bank << 15) + 0x0000);
				MEM_PTR(bank, 0x2000) = MEM_PTR(0x80 + bank, 0x2000) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + (bank << 15) + 0x2000);
				MEM_PTR(bank, 0x4000) = MEM_PTR(0x80 + bank, 0x4000) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + (bank << 15) + 0x4000);
				MEM_PTR(bank, 0x6000) = MEM_PTR(0x80 + bank, 0x6000) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + (bank << 15) + 0x6000);
			}

			MEM_PTR(bank, 0x8000) = MEM_PTR(0x80 + bank, 0x8000) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + (bank << 15) + 0x0000);
			MEM_PTR(bank, 0xA000) = MEM_PTR(0x80 + bank, 0xA000) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + (bank << 15) + 0x2000);
			MEM_PTR(bank, 0xC000) = MEM_PTR(0x80 + bank, 0xC000) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + (bank << 15) + 0x4000);
			MEM_PTR(bank, 0xE000) = MEM_PTR(0x80 + bank, 0xE000) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + (bank << 15) + 0x6000);
		}
	}
}

void _ROM_DoCacheBank(int bank, int type, bool force)
{
	u8 idx;
	
	bank &= 0x7F;
	if (Mem_HiROM && bank < 0x40) bank += 0x40;
	
	if (bank >= (Mem_HiROM ? 0x60 : 0x20))
	{
		if (type == 1) idx = ROMCACHE_SIZE;
		else if (type == 2) idx = ROMCACHE_SIZE + 1;
		else return;
	}
	else if (force)
		idx = Mem_HiROM ? (bank - 0x40) : bank;
	else
		return;

	int oldbank = ROM_CacheBank[idx];
	if (oldbank == bank)
		return;
	if (oldbank != -1)
		ROM_DoUncacheBank(oldbank);

	ROM_CacheBank[idx] = bank;
	if (!ROM_Cache[idx])
		ROM_Cache[idx] = malloc(Mem_HiROM ? 0x10000 : 0x8000);

	u8* ptr = ROM_Cache[idx];
	u32 base = ROM_BaseOffset;
	if (Mem_HiROM)
	{
		fseek(ROM_File, base + ((bank - 0x40) << 16), SEEK_SET);
		fread(ptr, 0x10000, 1, ROM_File);
		
		if (bank < 0x7E)
		{
			MEM_PTR(bank, 0x0000) = MEM_PTR(0x80 + bank, 0x0000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0x0000];
			MEM_PTR(bank, 0x2000) = MEM_PTR(0x80 + bank, 0x2000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0x2000];
			MEM_PTR(bank, 0x4000) = MEM_PTR(0x80 + bank, 0x4000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0x4000];
			MEM_PTR(bank, 0x6000) = MEM_PTR(0x80 + bank, 0x6000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0x6000];
			MEM_PTR(bank, 0x8000) = MEM_PTR(0x80 + bank, 0x8000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0x8000];
			MEM_PTR(bank, 0xA000) = MEM_PTR(0x80 + bank, 0xA000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0xA000];
			MEM_PTR(bank, 0xC000) = MEM_PTR(0x80 + bank, 0xC000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0xC000];
			MEM_PTR(bank, 0xE000) = MEM_PTR(0x80 + bank, 0xE000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0xE000];
		}
		else
		{
			MEM_PTR(0x80 + bank, 0x0000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0x0000];
			MEM_PTR(0x80 + bank, 0x2000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0x2000];
			MEM_PTR(0x80 + bank, 0x4000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0x4000];
			MEM_PTR(0x80 + bank, 0x6000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0x6000];
			MEM_PTR(0x80 + bank, 0x8000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0x8000];
			MEM_PTR(0x80 + bank, 0xA000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0xA000];
			MEM_PTR(0x80 + bank, 0xC000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0xC000];
			MEM_PTR(0x80 + bank, 0xE000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0xE000];
		}
		
		MEM_PTR(bank - 0x40, 0x8000) = MEM_PTR(0x40 + bank, 0x8000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0x8000];
		MEM_PTR(bank - 0x40, 0xA000) = MEM_PTR(0x40 + bank, 0xA000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0xA000];
		MEM_PTR(bank - 0x40, 0xC000) = MEM_PTR(0x40 + bank, 0xC000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0xC000];
		MEM_PTR(bank - 0x40, 0xE000) = MEM_PTR(0x40 + bank, 0xE000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0xE000];
	}
	else
	{
		if (bank < 0x7E)
		{
			fseek(ROM_File, base + (bank << 15), SEEK_SET);
			fread(ptr, 0x8000, 1, ROM_File);

			if (bank >= 0x40 && bank < 0x70)
			{
				MEM_PTR(bank, 0x0000) = MEM_PTR(0x80 + bank, 0x0000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0x0000];
				MEM_PTR(bank, 0x2000) = MEM_PTR(0x80 + bank, 0x2000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0x2000];
				MEM_PTR(bank, 0x4000) = MEM_PTR(0x80 + bank, 0x4000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0x4000];
				MEM_PTR(bank, 0x6000) = MEM_PTR(0x80 + bank, 0x6000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0x6000];
			}

			MEM_PTR(bank, 0x8000) = MEM_PTR(0x80 + bank, 0x8000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0x0000];
			MEM_PTR(bank, 0xA000) = MEM_PTR(0x80 + bank, 0xA000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0x2000];
			MEM_PTR(bank, 0xC000) = MEM_PTR(0x80 + bank, 0xC000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0x4000];
			MEM_PTR(bank, 0xE000) = MEM_PTR(0x80 + bank, 0xE000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0x6000];
		}
	}

	//idx++;
	//idx &= 7;
	//if (idx >= ROMCACHE_SIZE) idx = 0;
	//ROM_CacheIndex = idx;
}

void ROM_DoCacheBank(int bank, int type)
{
	asm("stmdb sp!, {r12}");
	_ROM_DoCacheBank(bank, type, false);
	asm("ldmia sp!, {r12}");
}


void ROM_ApplySpeedHacks()
{
	// TODO look into other banks? low priority I guess
	// most games would put their main loop into bank 0
	u8* bank = ROM_Bank0;
	int i;

	for (i = 2; i < 0x8000;)
	{
		//if (bank[i] == 0xA5 && bank[i+2] == 0xF0 && bank[i+3] == 0xFC)
		if (bank[i] == 0xA5 && (bank[i+2] & 0x1F) == 0x10 && bank[i+3] == 0xFC)
		{
			u8 branchtype = bank[i+2];
			bank[i+2] = 0x42;
			bank[i+3] = (bank[i+3] & 0x0F) | (branchtype & 0xF0);
			
			iprintf("Speed hack installed @ 80:%04X\n", 0x8000+i);
			
			i += 4;
		}
		else if (bank[i] == 0xAD && (bank[i+3] & 0x1F) == 0x10 && bank[i+4] == 0xFB)
		{
			u16 addr = bank[i+1] | (bank[i+2] << 8);
			
			if ((addr & 0xFFF0) != 0x2140)
			{
				u8 branchtype = bank[i+3];
				bank[i+3] = 0x42;
				bank[i+4] = (bank[i+4] & 0x0F) | (branchtype & 0xF0);
				
				iprintf("Speed hack installed @ 80:%04X\n", 0x8000+i);
			}
			
			i += 5;
		}
		else
			i++;
	}
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
	Mem_SRAMMask = sramsize ? ((1024 << sramsize) - 1) : 0;
	Mem_SRAMMask &= 0x000FFFFF;
	iprintf("SRAM size: %dKB\n", (Mem_SRAMMask+1) >> 10);
	
	strncpy(Mem_SRAMPath, path, strlen(path)-3);
	strncpy(Mem_SRAMPath + strlen(path)-3, "srm", 3);
	Mem_SRAMPath[strlen(path)] = '\0';
	FILE* sram = fopen(Mem_SRAMPath, "r+");
	if (!sram) sram = fopen(Mem_SRAMPath, "w+");
	if (sram) fclose(sram);
	
	return true;
}

void Mem_Reset()
{
	u32 i, a, b;

	for (i = 0; i < (128 * 1024); i += 4)
		*(u32*)&Mem_SysRAM[i] = 0x55555555; // idk about this

	//fseek(ROM_File, ROM_BaseOffset, SEEK_SET);
	//fread(ROM_Bank0, 0x8000, 1, ROM_File);
	
	if (!ROM_CacheInited)
	{
		for (i = 0; i < 32; i++)
			ROM_Cache[i] = 0;
		
		ROM_CacheInited = 0;
	}

	for (i = 0; i < 32 + 2; i++)
		ROM_CacheBank[i] = -1;
	ROM_CacheIndex = 0;

	if (Mem_SRAM) free(Mem_SRAM);
	Mem_SRAM = malloc(Mem_SRAMMask + 1);
	for (i = 0; i <= Mem_SRAMMask; i += 4)
		*(u32*)&Mem_SRAM[i] = 0;
		
	FILE* sram = fopen(Mem_SRAMPath, "r");
	if (sram)
	{
		fread(Mem_SRAM, Mem_SRAMMask+1, 1, sram);
		fclose(sram);
	}
		
	Mem_PtrTable = &_Mem_PtrTable[0x1];
	Mem_PtrTable[-0x1] = 0;
	
	for (b = 0; b < 0x40; b++)
	{
		MEM_PTR(b, 0x0000) = MEM_PTR(0x80 + b, 0x0000) = MPTR_SLOW | (u32)&Mem_SysRAM[0];
		MEM_PTR(b, 0x2000) = MEM_PTR(0x80 + b, 0x2000) = MPTR_SPECIAL;
		MEM_PTR(b, 0x4000) = MEM_PTR(0x80 + b, 0x4000) = MPTR_SPECIAL;
		
		if (Mem_HiROM)
			MEM_PTR(b, 0x6000) = MEM_PTR(0x80 + b, 0x6000) = MPTR_SLOW | MPTR_SRAM | (u32)&Mem_SRAM[(b << 13) & Mem_SRAMMask];
		else
			MEM_PTR(b, 0x6000) = MEM_PTR(0x80 + b, 0x6000) = MPTR_SLOW | MPTR_SPECIAL;
	}

	//for (a = 0; a < 0x8000; a += 0x2000)
	//	MEM_PTR(0, 0x8000 + a) = MEM_PTR(0x80, 0x8000 + a) = MPTR_SLOW | MPTR_READONLY | (u32)&ROM_Bank0[a];

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

		for (b = 0; b < 0x0E; b++)
			for (a = 0; a < 0x8000; a += 0x2000)
				MEM_PTR(0x70 + b, a) = MEM_PTR(0xF0 + b, a) = MPTR_SLOW | MPTR_SRAM | (u32)&Mem_SRAM[((b << 15) + a) & Mem_SRAMMask];

		for (b = 0; b < 0x02; b++)
			for (a = 0; a < 0x10000; a += 0x2000)
				MEM_PTR(0x7E + b, a) = MEM_PTR(0xFE + b, a) = MPTR_SLOW | (u32)&Mem_SysRAM[(b << 16) + a];
	}
	
	for (i = 0; i < 32; i++)
	{
		u32 fofs = Mem_HiROM ? (i << 16) : (i << 15);
		if (fofs >= ROM_FileSize - ROM_BaseOffset)
			break;
		
		_ROM_DoCacheBank(i, 0, true);
	}
	
	ROM_Bank0 = ROM_Cache[0];
	ROM_Bank0End = Mem_HiROM ? (ROM_Bank0 + 0x10000) : (ROM_Bank0 + 0x8000);
	
	ROM_ApplySpeedHacks();
	
	iprintf("sysram = %08X\n", &Mem_SysRAM[0]);
	
	// get uncached address
	u32 ipcsize = (sizeof(IPCStruct) + 0x1F) & ~0x1F;
	IPC = memalign(32, ipcsize);
	DC_InvalidateRange(IPC, ipcsize);
	IPC = memUncached(IPC);
	iprintf("IPC struct = %08X\n", IPC);
	fifoSendValue32(FIFO_USER_01, 3);
	fifoSendAddress(FIFO_USER_01, IPC);
	
	Mem_HVBJOY = 0x00;
	
	Mem_MulA = 0;
	Mem_MulRes = 0;
	Mem_DivA = 0;
	Mem_DivRes = 0;
	
	PPU_Reset();
}


void Mem_SaveSRAM()
{
	if (!Mem_PtrTable[-0x1])
		return;
	
	FILE* sram = fopen(Mem_SRAMPath, "r+");
	if (sram)
	{
		iprintf("SRAM save\n");
		Mem_PtrTable[-0x1] = 0;
		fseek(sram, 0, SEEK_SET);
		fwrite(Mem_SRAM, Mem_SRAMMask+1, 1, sram);
		fclose(sram);
	}
}


u32 ROM_ReadBuffer;

// (slow) uncached ROM read
// potential optimization: detect sequential reads to avoid
// seeking every time
ITCM_CODE u8 Mem_ROMRead8(u32 fileaddr)
{
	asm("stmdb sp!, {r1-r3, r12}");

	if (fileaddr < ROM_FileSize)
	{
		fseek(ROM_File, fileaddr, SEEK_SET);
		fread(&ROM_ReadBuffer, 1, 1, ROM_File);
	}
	else
		ROM_ReadBuffer = 0;

	asm("ldmia sp!, {r1-r3, r12}");
	return ROM_ReadBuffer & 0xFF;
}

ITCM_CODE u16 Mem_ROMRead16(u32 fileaddr)
{
	asm("stmdb sp!, {r1-r3, r12}");

	if (fileaddr < ROM_FileSize)
	{
		fseek(ROM_File, fileaddr, SEEK_SET);
		fread(&ROM_ReadBuffer, 2, 1, ROM_File);
	}
	else
		ROM_ReadBuffer = 0;
	
	asm("ldmia sp!, {r1-r3, r12}");
	return ROM_ReadBuffer & 0xFFFF;
}

ITCM_CODE u32 Mem_ROMRead24(u32 fileaddr)
{
	asm("stmdb sp!, {r1-r3, r12}");

	if (fileaddr < ROM_FileSize)
	{
		fseek(ROM_File, fileaddr, SEEK_SET);
		fread(&ROM_ReadBuffer, 3, 1, ROM_File);
	}
	else
		ROM_ReadBuffer = 0;

	asm("ldmia sp!, {r1-r3, r12}");
	return ROM_ReadBuffer & 0x00FFFFFF;
}


ITCM_CODE void report_unk_lol(u32 op, u32 pc)
{
	if (op == 0xDB) 
	{
		asm("stmdb sp!, {r12}");
		printf("STOP %06X\n", pc);
		asm("ldmia sp!, {r12}");
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


u8 Mem_GIORead8(u32 addr)
{
	asm("stmdb sp!, {r12}");
	
	u8 ret = 0;
	switch (addr)
	{
		case 0x10:
			//iprintf("!! READ 4210 -- ACK NMI\n");
			break;
			
		case 0x12:
			ret = Mem_HVBJOY;
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
	
	asm("ldmia sp!, {r12}");
	return ret;
}

u16 Mem_GIORead16(u32 addr)
{
	asm("stmdb sp!, {r12}");
	
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
	}
	
	asm("ldmia sp!, {r12}");
	return ret;
}

void Mem_GIOWrite8(u32 addr, u8 val)
{
	asm("stmdb sp!, {r12}");
	
	switch (addr)
	{
		case 0x00:
			if (val & 0x10) iprintf("HCOUNT IRQ ENABLE: %02X\n", val);
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
			
		case 0x09:
			Mem_VMatch &= 0xFF00;
			Mem_VMatch |= val;
			break;
		case 0x0A:
			Mem_VMatch &= 0x00FF;
			Mem_VMatch |= (val << 8);
			break;
			
		case 0x0B:
			DMA_Enable(val);
			break;
	}
	
	asm("ldmia sp!, {r12}");
}

void Mem_GIOWrite16(u32 addr, u16 val)
{
	asm("stmdb sp!, {r12}");
	
	switch (addr)
	{
		case 0x09:
			Mem_VMatch = val;
			break;
	}
	
	asm("ldmia sp!, {r12}");
}


u8 Mem_Read8(u32 addr)
{
	u32 ptr = Mem_PtrTable[addr >> 13];
	if (ptr & MPTR_SPECIAL)
	{
		if (ptr & MPTR_READONLY)
		{
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
