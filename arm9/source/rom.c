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


// ROM buffer -- max size: 2MB
// ROMs <= 2MB: all ROM resident
// ROMs > 2MB: 1MB resident, 1MB dynamic cache
u8* ROM_Buffer;
u32 ROM_BufferSize;

u32 ROM_BankSize;
u32 ROM_NumBanks;

// bit0-7 tell which bank the chunk is mapped to
// bit8-15 tell what the chunk is used for
// also serves as priority, ie a resident chunk can't be reused for dynamic caching
// 0xFF: resident
// 0x81: PBR
// 0x80: DBR
// 0x40: successive cache misses
// 0x00: none
u32 ROM_NumChunks;
u16* ROM_ChunkUsage;
u32 ROM_CurChunk;

// 0xFF: not cached
// otherwise chunk index
u8 ROM_BankStatus[0x80];

u8 ROM_CacheMisses[0x200];

u32 ROM_FileOffset;


// TODO find a better way to do speedhacks
// (like, detecting branches with offset -4 or -5 at runtime)
void ROM_ApplySpeedHacks(int banknum, u8* bank)
{
	int i;
	int bsize = Mem_HiROM ? 0x10000 : 0x8000;

	for (i = 2; i < bsize;)
	{
		//if (bank[i] == 0xA5 && bank[i+2] == 0xF0 && bank[i+3] == 0xFC)
		if (bank[i] == 0xA5 && (bank[i+2] & 0x1F) == 0x10 && bank[i+3] == 0xFC)
		{
			u8 branchtype = bank[i+2];
			bank[i+2] = 0x42;
			bank[i+3] = (bank[i+3] & 0x0F) | (branchtype & 0xF0);
			
			//iprintf("Speed hack installed @ %02X:%04X\n", banknum, (Mem_HiROM?0:0x8000)+i);
			
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
				
				//iprintf("Speed hack installed @ %02X:%04X\n", banknum, (Mem_HiROM?0:0x8000)+i);
			}
			
			i += 5;
		}
		else
			i++;
	}
}


void ROM_MapBankToRAM(u32 bank, u8* ptr)
{
	ROM_FileOffset = -1;
	u32 hi_slow = Mem_FastROM ? 0 : MPTR_SLOW;
	
	u32 old_ime = REG_IME;
	REG_IME = 0;
	
	u32 base = ROM_BaseOffset;
	if (Mem_HiROM)
	{
		fseek(ROM_File, base + ((bank - 0x40) << 16), SEEK_SET);
		fread(ptr, 0x10000, 1, ROM_File);
		
		for (; bank < 0x80; bank += ROM_NumBanks)
		{
			if (bank < 0x7E)
			{
				MEM_PTR(bank, 0x0000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0x0000];
				MEM_PTR(bank, 0x2000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0x2000];
				MEM_PTR(bank, 0x4000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0x4000];
				MEM_PTR(bank, 0x6000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0x6000];
				MEM_PTR(bank, 0x8000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0x8000];
				MEM_PTR(bank, 0xA000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0xA000];
				MEM_PTR(bank, 0xC000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0xC000];
				MEM_PTR(bank, 0xE000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0xE000];
			}
			
			MEM_PTR(0x80 + bank, 0x0000) = hi_slow | MPTR_READONLY | (u32)&ptr[0x0000];
			MEM_PTR(0x80 + bank, 0x2000) = hi_slow | MPTR_READONLY | (u32)&ptr[0x2000];
			MEM_PTR(0x80 + bank, 0x4000) = hi_slow | MPTR_READONLY | (u32)&ptr[0x4000];
			MEM_PTR(0x80 + bank, 0x6000) = hi_slow | MPTR_READONLY | (u32)&ptr[0x6000];
			MEM_PTR(0x80 + bank, 0x8000) = hi_slow | MPTR_READONLY | (u32)&ptr[0x8000];
			MEM_PTR(0x80 + bank, 0xA000) = hi_slow | MPTR_READONLY | (u32)&ptr[0xA000];
			MEM_PTR(0x80 + bank, 0xC000) = hi_slow | MPTR_READONLY | (u32)&ptr[0xC000];
			MEM_PTR(0x80 + bank, 0xE000) = hi_slow | MPTR_READONLY | (u32)&ptr[0xE000];
			
			MEM_PTR(bank - 0x40, 0x8000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0x8000];
			MEM_PTR(bank - 0x40, 0xA000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0xA000];
			MEM_PTR(bank - 0x40, 0xC000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0xC000];
			MEM_PTR(bank - 0x40, 0xE000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0xE000];
			
			MEM_PTR(0x40 + bank, 0x8000) = hi_slow | MPTR_READONLY | (u32)&ptr[0x8000];
			MEM_PTR(0x40 + bank, 0xA000) = hi_slow | MPTR_READONLY | (u32)&ptr[0xA000];
			MEM_PTR(0x40 + bank, 0xC000) = hi_slow | MPTR_READONLY | (u32)&ptr[0xC000];
			MEM_PTR(0x40 + bank, 0xE000) = hi_slow | MPTR_READONLY | (u32)&ptr[0xE000];
		}
	}
	else
	{
		fseek(ROM_File, base + (bank << 15), SEEK_SET);
		fread(ptr, 0x8000, 1, ROM_File);

		for (; bank < 0x80; bank += ROM_NumBanks)
		{
			if (bank >= 0x40 && bank < 0x70)
			{
				MEM_PTR(bank, 0x0000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0x0000];
				MEM_PTR(bank, 0x2000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0x2000];
				MEM_PTR(bank, 0x4000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0x4000];
				MEM_PTR(bank, 0x6000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0x6000];
				
				MEM_PTR(0x80 + bank, 0x0000) = hi_slow | MPTR_READONLY | (u32)&ptr[0x0000];
				MEM_PTR(0x80 + bank, 0x2000) = hi_slow | MPTR_READONLY | (u32)&ptr[0x2000];
				MEM_PTR(0x80 + bank, 0x4000) = hi_slow | MPTR_READONLY | (u32)&ptr[0x4000];
				MEM_PTR(0x80 + bank, 0x6000) = hi_slow | MPTR_READONLY | (u32)&ptr[0x6000];
			}
			
			if (bank < 0x7E)
			{
				MEM_PTR(bank, 0x8000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0x0000];
				MEM_PTR(bank, 0xA000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0x2000];
				MEM_PTR(bank, 0xC000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0x4000];
				MEM_PTR(bank, 0xE000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0x6000];
			}
			
			MEM_PTR(0x80 + bank, 0x8000) = hi_slow | MPTR_READONLY | (u32)&ptr[0x0000];
			MEM_PTR(0x80 + bank, 0xA000) = hi_slow | MPTR_READONLY | (u32)&ptr[0x2000];
			MEM_PTR(0x80 + bank, 0xC000) = hi_slow | MPTR_READONLY | (u32)&ptr[0x4000];
			MEM_PTR(0x80 + bank, 0xE000) = hi_slow | MPTR_READONLY | (u32)&ptr[0x6000];
		}
	}
	
	ROM_ApplySpeedHacks(bank, ptr);
	
	REG_IME = old_ime;
}

void ROM_MapBankToFile(u32 bank)
{
	u32 hi_slow = Mem_FastROM ? 0 : MPTR_SLOW;
	
	if (Mem_HiROM)
	{
		u32 b = (bank - 0x40) << 16;
		
		for (; bank < 0x80; bank += ROM_NumBanks)
		{
			if (bank < 0x7E)
			{
				MEM_PTR(bank, 0x0000) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + b + 0x0000);
				MEM_PTR(bank, 0x2000) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + b + 0x2000);
				MEM_PTR(bank, 0x4000) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + b + 0x4000);
				MEM_PTR(bank, 0x6000) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + b + 0x6000);
				MEM_PTR(bank, 0x8000) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + b + 0x8000);
				MEM_PTR(bank, 0xA000) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + b + 0xA000);
				MEM_PTR(bank, 0xC000) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + b + 0xC000);
				MEM_PTR(bank, 0xE000) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + b + 0xE000);
			}

			MEM_PTR(0x80 + bank, 0x0000) = hi_slow | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + b + 0x0000);
			MEM_PTR(0x80 + bank, 0x2000) = hi_slow | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + b + 0x2000);
			MEM_PTR(0x80 + bank, 0x4000) = hi_slow | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + b + 0x4000);
			MEM_PTR(0x80 + bank, 0x6000) = hi_slow | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + b + 0x6000);
			MEM_PTR(0x80 + bank, 0x8000) = hi_slow | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + b + 0x8000);
			MEM_PTR(0x80 + bank, 0xA000) = hi_slow | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + b + 0xA000);
			MEM_PTR(0x80 + bank, 0xC000) = hi_slow | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + b + 0xC000);
			MEM_PTR(0x80 + bank, 0xE000) = hi_slow | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + b + 0xE000);
			
			MEM_PTR(bank - 0x40, 0x8000) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + b + 0x8000);
			MEM_PTR(bank - 0x40, 0xA000) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + b + 0xA000);
			MEM_PTR(bank - 0x40, 0xC000) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + b + 0xC000);
			MEM_PTR(bank - 0x40, 0xE000) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + b + 0xE000);
			
			MEM_PTR(0x40 + bank, 0x8000) = hi_slow | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + b + 0x8000);
			MEM_PTR(0x40 + bank, 0xA000) = hi_slow | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + b + 0xA000);
			MEM_PTR(0x40 + bank, 0xC000) = hi_slow | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + b + 0xC000);
			MEM_PTR(0x40 + bank, 0xE000) = hi_slow | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + b + 0xE000);
		}
	}
	else
	{
		u32 b = bank << 15;
		
		for (; bank < 0x80; bank += ROM_NumBanks)
		{
			if (bank >= 0x40 && bank < 0x70)
			{
				MEM_PTR(bank, 0x0000) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + b + 0x0000);
				MEM_PTR(bank, 0x2000) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + b + 0x2000);
				MEM_PTR(bank, 0x4000) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + b + 0x4000);
				MEM_PTR(bank, 0x6000) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + b + 0x6000);
				
				MEM_PTR(0x80 + bank, 0x0000) = hi_slow | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + b + 0x0000);
				MEM_PTR(0x80 + bank, 0x2000) = hi_slow | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + b + 0x2000);
				MEM_PTR(0x80 + bank, 0x4000) = hi_slow | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + b + 0x4000);
				MEM_PTR(0x80 + bank, 0x6000) = hi_slow | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + b + 0x6000);
			}

			if (bank < 0x7E)
			{
				MEM_PTR(bank, 0x8000) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + b + 0x0000);
				MEM_PTR(bank, 0xA000) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + b + 0x2000);
				MEM_PTR(bank, 0xC000) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + b + 0x4000);
				MEM_PTR(bank, 0xE000) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + b + 0x6000);
			}
			
			MEM_PTR(0x80 + bank, 0x8000) = hi_slow | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + b + 0x0000);
			MEM_PTR(0x80 + bank, 0xA000) = hi_slow | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + b + 0x2000);
			MEM_PTR(0x80 + bank, 0xC000) = hi_slow | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + b + 0x4000);
			MEM_PTR(0x80 + bank, 0xE000) = hi_slow | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + b + 0x6000);
		}
	}
}


void ROM_CacheBank(u32 bank, u32 type)
{
	bank &= (ROM_NumBanks - 1);
	if (Mem_HiROM && bank < 0x40) bank += 0x40;
	
	//iprintf("cache bank %02X %02X %02X\n", bank, type, ROM_BankStatus[bank]);
	
	// the first half of the chunks will alawys be used for resident banks
	u32 firstchunk = (type == 0xFF) ? 0 : (ROM_NumChunks >> 1);
	
	// bank already cached
	if (ROM_BankStatus[bank] != 0xFF)
	{
		u8 chunk = ROM_BankStatus[bank];
		if ((ROM_ChunkUsage[chunk] >> 8) < type)
		{
			ROM_ChunkUsage[chunk] &= 0x00FF;
			ROM_ChunkUsage[chunk] |= (type << 8);
		}
		
		return;
	}
	
	for (;;)
	{
		u32 ctype = ROM_ChunkUsage[ROM_CurChunk] >> 8;
		bool canremap;
		if (ctype == 0xFF) canremap = false;
		else if (type & 0x80) canremap = ((!(ctype & 0x80)) || (ctype == type));
		else canremap = (ctype <= type);
		
		if (!canremap)
		{
			ROM_CurChunk++;
			if (ROM_CurChunk >= ROM_NumChunks)
				ROM_CurChunk = firstchunk;
			
			continue;
		}
		
		if (ctype)
		{
			u32 oldbank = ROM_ChunkUsage[ROM_CurChunk] & 0xFF;
			ROM_MapBankToFile(oldbank);
			ROM_BankStatus[oldbank] = 0xFF;
		}
		
		break;
	}
	
	ROM_MapBankToRAM(bank, &ROM_Buffer[ROM_CurChunk * ROM_BankSize]);
	ROM_ChunkUsage[ROM_CurChunk] = (type << 8) | bank;
	ROM_BankStatus[bank] = ROM_CurChunk;
	
	ROM_CurChunk++;
	if (ROM_CurChunk >= ROM_NumChunks)
		ROM_CurChunk = firstchunk;
}

void ROM_DoCacheBank(u32 bank, u32 type)
{
	asm("stmdb sp!, {r12}");
	if (bank != 0x7E && bank != 0x7F)
		ROM_CacheBank(bank, type);
	asm("ldmia sp!, {r12}");
}


void ROM_SpeedChanged()
{
	u32 b, a;
	
	if (Mem_FastROM)
	{
		iprintf("Fast ROM\n");
		
		for (b = 0x80; b < 0xC0; b++)
			for (a = 0x8000; a < 0x10000; a += 0x2000)
				MEM_PTR(b, a) &= ~MPTR_SLOW;
				
		for (b = 0xC0; b < 0x100; b++)
			for (a = 0x0000; a < 0x10000; a += 0x2000)
				MEM_PTR(b, a) &= ~MPTR_SLOW;
	}
	else
	{
		iprintf("Slow ROM\n");
		
		for (b = 0x80; b < 0xC0; b++)
			for (a = 0x8000; a < 0x10000; a += 0x2000)
				MEM_PTR(b, a) |= MPTR_SLOW;
				
		for (b = 0xC0; b < 0x100; b++)
			for (a = 0x0000; a < 0x10000; a += 0x2000)
				MEM_PTR(b, a) |= MPTR_SLOW;
	}
}


void ROM_SetupCache()
{
	u32 i;
	
	u32 romsize = ROM_FileSize - ROM_BaseOffset;
	ROM_BufferSize = romsize;
	ROM_BankSize = Mem_HiROM ? 0x10000 : 0x8000;
	
	u32 nbanks = romsize >> (Mem_HiROM ? 16:15);
	ROM_NumBanks = 1;
	while (ROM_NumBanks < nbanks) ROM_NumBanks <<= 1;
	
	ROM_BufferSize = (ROM_BufferSize + ROM_BankSize - 1) & (~(ROM_BankSize - 1));
	
	// limit buffer size to 2MB
	// TODO higher limit for DSi mode (whole ROM can fit in RAM)
	if (ROM_BufferSize > 0x200000) ROM_BufferSize = 0x200000;
	
	iprintf("ROM buffer size: %dKB\n", ROM_BufferSize >> 10);
	ROM_Buffer = (u8*)malloc(ROM_BufferSize);
	if (!ROM_Buffer)
	{
		iprintf("Allocation failed\n");
		for(;;);
	}
	
	ROM_NumChunks = ROM_BufferSize >> (Mem_HiROM ? 16:15);
	ROM_ChunkUsage = (u16*)malloc(ROM_NumChunks * sizeof(u16));
	for (i = 0; i < ROM_NumChunks; i++)
		ROM_ChunkUsage[i] = 0;
		
	ROM_CurChunk = 0;
	
	for (i = 0; i < 0x80; i++)
		ROM_BankStatus[i] = 0xFF;
		
	u32 max_resident = (romsize > 0x200000) ? 0x100000 : romsize;
	u32 b = Mem_HiROM ? 0x40 : 0x00;
	for (i = 0; i < max_resident; i += ROM_BankSize)
	{
		ROM_CacheBank(b, 0xFF);
		b++;
	}
	
	for (i = 0; i < 0x200; i++)
		ROM_CacheMisses[i] = 0;
		
	ROM_Bank0 = ROM_Buffer;
	ROM_Bank0End = ROM_Bank0 + ROM_BankSize;
	
	ROM_FileOffset = -1;
}


u32 ROM_ReadBuffer;

void ROM_CacheMiss(u32 addr)
{
	u32 bank = addr >> (Mem_HiROM ? 16 : 15);
	
	ROM_CacheMisses[bank]++;
	if (ROM_CacheMisses[bank] > 4)
	{
		ROM_CacheMisses[bank] = 0;
		ROM_CacheBank(bank, 0x40);
	}
}

// (slow) uncached ROM read
ITCM_CODE u8 Mem_ROMRead8(u32 fileaddr)
{
	asm("stmdb sp!, {r1-r3, r12}");

	if (fileaddr < ROM_FileSize)
	{
		if (fileaddr != ROM_FileOffset)
		{
			ROM_FileOffset = fileaddr;
			fseek(ROM_File, fileaddr, SEEK_SET);
		}
		
		fread(&ROM_ReadBuffer, 1, 1, ROM_File);
		ROM_FileOffset++;
	}
	else
		ROM_ReadBuffer = 0;
	
	ROM_CacheMiss(fileaddr - ROM_BaseOffset);

	asm("ldmia sp!, {r1-r3, r12}");
	return ROM_ReadBuffer & 0xFF;
}

ITCM_CODE u16 Mem_ROMRead16(u32 fileaddr)
{
	asm("stmdb sp!, {r1-r3, r12}");

	if (fileaddr < ROM_FileSize)
	{
		if (fileaddr != ROM_FileOffset)
		{
			ROM_FileOffset = fileaddr;
			fseek(ROM_File, fileaddr, SEEK_SET);
		}
		
		fread(&ROM_ReadBuffer, 2, 1, ROM_File);
		ROM_FileOffset += 2;
	}
	else
		ROM_ReadBuffer = 0;
	
	ROM_CacheMiss(fileaddr - ROM_BaseOffset);
	
	asm("ldmia sp!, {r1-r3, r12}");
	return ROM_ReadBuffer & 0xFFFF;
}

ITCM_CODE u32 Mem_ROMRead24(u32 fileaddr)
{
	asm("stmdb sp!, {r1-r3, r12}");

	if (fileaddr < ROM_FileSize)
	{
		if (fileaddr != ROM_FileOffset)
		{
			ROM_FileOffset = fileaddr;
			fseek(ROM_File, fileaddr, SEEK_SET);
		}
		
		fread(&ROM_ReadBuffer, 3, 1, ROM_File);
		ROM_FileOffset += 3;
	}
	else
		ROM_ReadBuffer = 0;
		
	ROM_CacheMiss(fileaddr - ROM_BaseOffset);

	asm("ldmia sp!, {r1-r3, r12}");
	return ROM_ReadBuffer & 0x00FFFFFF;
}
