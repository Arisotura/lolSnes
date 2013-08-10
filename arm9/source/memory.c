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
u32 Mem_PtrTable[0x800] DTCM_BSS;
// memory timings (6 or 8 master cycles)
//u8 Mem_TimingTable[0x800] DTCM_BSS;


u8 _SPC_IOPorts[8] = {0,0,0,0, 0,0,0,0};
u8* SPC_IOPorts;


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
	if (Mem_HiROM)
	{
		if (bank < 0x40)
		{
			MEM_PTR(bank, 0x8000) = MEM_PTR(0x80 + bank, 0x8000) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + (bank << 15) + 0x0000);
			MEM_PTR(bank, 0xA000) = MEM_PTR(0x80 + bank, 0xA000) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + (bank << 15) + 0x2000);
			MEM_PTR(bank, 0xC000) = MEM_PTR(0x80 + bank, 0xC000) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + (bank << 15) + 0x4000);
			MEM_PTR(bank, 0xE000) = MEM_PTR(0x80 + bank, 0xE000) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + (bank << 15) + 0x6000);
		}
		else if (bank < 0x7E)
		{
			bank -= 0x40;

			MEM_PTR(bank, 0x0000) = MEM_PTR(0x80 + bank, 0x0000) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + (bank << 16) + 0x0000);
			MEM_PTR(bank, 0x2000) = MEM_PTR(0x80 + bank, 0x2000) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + (bank << 16) + 0x2000);
			MEM_PTR(bank, 0x4000) = MEM_PTR(0x80 + bank, 0x4000) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + (bank << 16) + 0x4000);
			MEM_PTR(bank, 0x6000) = MEM_PTR(0x80 + bank, 0x6000) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + (bank << 16) + 0x6000);
			MEM_PTR(bank, 0x8000) = MEM_PTR(0x80 + bank, 0x8000) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + (bank << 16) + 0x8000);
			MEM_PTR(bank, 0xA000) = MEM_PTR(0x80 + bank, 0xA000) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + (bank << 16) + 0xA000);
			MEM_PTR(bank, 0xC000) = MEM_PTR(0x80 + bank, 0xC000) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + (bank << 16) + 0xC000);
			MEM_PTR(bank, 0xE000) = MEM_PTR(0x80 + bank, 0xE000) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + (bank << 16) + 0xE000);
		}
		else
		{
			bank -= 0xC0;

			MEM_PTR(bank, 0x0000) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + (bank << 16) + 0x0000);
			MEM_PTR(bank, 0x2000) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + (bank << 16) + 0x2000);
			MEM_PTR(bank, 0x4000) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + (bank << 16) + 0x4000);
			MEM_PTR(bank, 0x6000) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + (bank << 16) + 0x6000);
			MEM_PTR(bank, 0x8000) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + (bank << 16) + 0x8000);
			MEM_PTR(bank, 0xA000) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + (bank << 16) + 0xA000);
			MEM_PTR(bank, 0xC000) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + (bank << 16) + 0xC000);
			MEM_PTR(bank, 0xE000) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + (bank << 16) + 0xE000);
		}
	}
	else
	{
		bank &= 0x7F;

		if (bank >= 0x40)
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

void _ROM_DoCacheBank(int bank, int type)
{
	u8 idx;
	if (bank >= 32)
	{
		if (type == 1) idx = ROMCACHE_SIZE;
		else if (type == 2) idx = ROMCACHE_SIZE + 1;
		else return;
	}
	else
		idx = bank;

	int oldbank = ROM_CacheBank[idx];
	if (oldbank == bank)
		return;
	if (oldbank != -1)
		ROM_DoUncacheBank(oldbank);

	ROM_CacheBank[idx] = bank;
	if (!ROM_Cache[idx])
		ROM_Cache[idx] = malloc((Mem_HiROM && bank >= 0x40) ? 0x10000 : 0x8000);

	u8* ptr = ROM_Cache[idx];
	u32 base = ROM_BaseOffset;
	if (Mem_HiROM)
	{
		if (bank < 0x40)
		{
			fseek(ROM_File, base + (bank << 15), SEEK_SET);
			fread(ptr, 0x8000, 1, ROM_File);
			
			MEM_PTR(bank, 0x8000) = MEM_PTR(0x80 + bank, 0x8000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0x0000];
			MEM_PTR(bank, 0xA000) = MEM_PTR(0x80 + bank, 0xA000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0x2000];
			MEM_PTR(bank, 0xC000) = MEM_PTR(0x80 + bank, 0xC000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0x4000];
			MEM_PTR(bank, 0xE000) = MEM_PTR(0x80 + bank, 0xE000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0x6000];
		}
		else if (bank < 0x7E)
		{
			fseek(ROM_File, base + ((bank - 0x40) << 16), SEEK_SET);
			fread(ptr, 0x10000, 1, ROM_File);

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
			fseek(ROM_File, base + ((bank - 0xC0) << 16), SEEK_SET);
			fread(ptr, 0x10000, 1, ROM_File);

			MEM_PTR(bank, 0x0000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0x0000];
			MEM_PTR(bank, 0x2000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0x2000];
			MEM_PTR(bank, 0x4000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0x4000];
			MEM_PTR(bank, 0x6000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0x6000];
			MEM_PTR(bank, 0x8000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0x8000];
			MEM_PTR(bank, 0xA000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0xA000];
			MEM_PTR(bank, 0xC000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0xC000];
			MEM_PTR(bank, 0xE000) = MPTR_SLOW | MPTR_READONLY | (u32)&ptr[0xE000];
		}
	}
	else
	{
		bank &= 0x7F;

		if (bank < 0x7E)
		{
			fseek(ROM_File, base + (bank << 15), SEEK_SET);
			fread(ptr, 0x8000, 1, ROM_File);

			if (bank >= 0x40)
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
	asm("ldmia sp!, {r12}");
	_ROM_DoCacheBank(bank, type);
	asm("stmdb sp!, {r12}");
}


bool Mem_LoadROM(char* path)
{
	FILE* oldfile = ROM_File;
	if (ROM_File != NULL)
		fclose(ROM_File);
	
	ROM_File = fopen(path, "rb");
	if (!ROM_File) return false;

	fseek(ROM_File, 0, SEEK_END);
	ROM_FileSize = ftell(ROM_File);
	
	if (ROM_CheckHeader(0x81C0) || ROM_FileSize == 0x8200) // headered, LoROM
	{
		ROM_BaseOffset = 0x200;
		Mem_HiROM = false;
	}
	else if (ROM_CheckHeader(0x101C0)) // headered, HiROM
	{
		ROM_BaseOffset = 0x200;
		Mem_HiROM = true;
	}
	else if (ROM_CheckHeader(0x7FC0) || ROM_FileSize == 0x8000) // headerless, LoROM
	{
		ROM_BaseOffset = 0;
		Mem_HiROM = false;
	}
	else if (ROM_CheckHeader(0xFFC0)) // headerless, HiROM
	{
		ROM_BaseOffset = 0;
		Mem_HiROM = true;
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
	}

	fseek(ROM_File, ROM_HeaderOffset + 0x18, SEEK_SET);
	u8 sramsize; fread(&sramsize, 1, 1, ROM_File);
	Mem_SRAMMask = sramsize ? ((1024 << sramsize) - 1) : 0;
	
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

	for (i = 0; i < 32; i++)
		ROM_CacheBank[i] = -1;
	ROM_CacheIndex = 0;

	if (Mem_SRAM) free(Mem_SRAM);
	Mem_SRAM = malloc(Mem_SRAMMask + 1);
	for (i = 0; i <= Mem_SRAMMask; i += 4)
		*(u32*)&Mem_SRAM[i] = 0;
	
	for (b = 0; b < 0x40; b++)
	{
		MEM_PTR(b, 0x0000) = MEM_PTR(0x80 + b, 0x0000) = MPTR_SLOW | (u32)&Mem_SysRAM[0];
		MEM_PTR(b, 0x2000) = MEM_PTR(0x80 + b, 0x2000) = MPTR_SPECIAL;
		MEM_PTR(b, 0x4000) = MEM_PTR(0x80 + b, 0x4000) = MPTR_SPECIAL;
		
		if (Mem_HiROM)
			MEM_PTR(b, 0x6000) = MEM_PTR(0x80 + b, 0x6000) = MPTR_SLOW | (u32)&Mem_SRAM[(b << 13) & Mem_SRAMMask];
		else
			MEM_PTR(b, 0x6000) = MEM_PTR(0x80 + b, 0x6000) = MPTR_SLOW | MPTR_SPECIAL;
	}

	//for (a = 0; a < 0x8000; a += 0x2000)
	//	MEM_PTR(0, 0x8000 + a) = MEM_PTR(0x80, 0x8000 + a) = MPTR_SLOW | MPTR_READONLY | (u32)&ROM_Bank0[a];

	for (b = 1; b < 0x40; b++)
	{
		for (a = 0; a < 0x8000; a += 0x2000)
			MEM_PTR(b, 0x8000 + a) = MEM_PTR(0x80 + b, 0x8000 + a) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + (b << 15) + a);
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
		for (b = 0; b < 0x3D; b++)
			for (a = 0; a < 0x10000; a += 0x2000)
				MEM_PTR(0x40 + b, a) = MEM_PTR(0xC0 + b, a) = MPTR_SLOW | MPTR_SPECIAL | MPTR_READONLY | (ROM_BaseOffset + 0x200000 + (b << 15) + (a & 0x7FFF));

		for (a = 0; a < 0x10000; a += 0x2000)
			MEM_PTR(0x7D, a) = MEM_PTR(0xFD, a) = MPTR_SLOW | (u32)&Mem_SRAM[a & Mem_SRAMMask];

		for (b = 0; b < 0x02; b++)
			for (a = 0; a < 0x10000; a += 0x2000)
				MEM_PTR(0x7E + b, a) = MEM_PTR(0xFE + b, a) = MPTR_SLOW | (u32)&Mem_SysRAM[(b << 16) + a];
	}
	
	for (i = 0; i < 32; i++)
	{
		if ((i << 15) >= ROM_FileSize - ROM_BaseOffset)
			break;
		
		_ROM_DoCacheBank(i, 0);
	}
	
	ROM_Bank0 = ROM_Cache[0];
	ROM_Bank0End = ROM_Bank0 + 0x8000;
	
	iprintf("mm %08X\n", (void*)Mem_ROMRead16);
	iprintf("sysram = %08X | %08X\n", &Mem_SysRAM[0], MEM_PTR(0x7F, 0x8000));
	
	// get uncached address
	SPC_IOPorts = (u8*)((u32)(&_SPC_IOPorts[0]) | 0x00400000);
	iprintf("SPC IO = %08X\n", SPC_IOPorts);
	fifoSendValue32(FIFO_USER_01, 3);
	fifoSendAddress(FIFO_USER_01, SPC_IOPorts);
	
	PPU_Reset();
}


u32 ROM_ReadBuffer;

// (slow) uncached ROM read
// potential optimization: detect sequential reads to avoid
// seeking every time
ITCM_CODE u8 Mem_ROMRead8(u32 fileaddr)
{
	if (fileaddr >= ROM_FileSize)
		return 0;
	
	asm("stmdb sp!, {r12}");

	fseek(ROM_File, fileaddr, SEEK_SET);
	fread(&ROM_ReadBuffer, 1, 1, ROM_File);

	asm("ldmia sp!, {r12}");
	return ROM_ReadBuffer & 0xFF;
}

ITCM_CODE u16 Mem_ROMRead16(u32 fileaddr)
{
	if (fileaddr >= ROM_FileSize)
		return 0;
	
	asm("stmdb sp!, {r12}");

	fseek(ROM_File, fileaddr, SEEK_SET);
	fread(&ROM_ReadBuffer, 2, 1, ROM_File);
	
	asm("ldmia sp!, {r12}");
	return ROM_ReadBuffer & 0xFFFF;
}

ITCM_CODE u32 Mem_ROMRead24(u32 fileaddr)
{
	if (fileaddr >= ROM_FileSize)
		return 0;
	
	asm("stmdb sp!, {r12}");

	fseek(ROM_File, fileaddr, SEEK_SET);
	fread(&ROM_ReadBuffer, 3, 1, ROM_File);

	asm("ldmia sp!, {r12}");
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


u8 Mem_GIORead8(u32 addr)
{
	asm("stmdb sp!, {r12}");
	
	u8 ret = 0;
	switch (addr)
	{
		case 0x18:
			ret = 0;	// todo
			break;
		case 0x19:
			{
				u16 keys = *(u16*)0x04000130;

				if (keys & 0x0004) ret |= 0x20;
				if (keys & 0x0008) ret |= 0x10;
			}
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
		case 0x18:
			{
				u16 keys = *(u16*)0x04000130;

				if (keys & 0x0004) ret |= 0x200000;
				if (keys & 0x0008) ret |= 0x100000;
			}
			break;
	}
	
	asm("ldmia sp!, {r12}");
	return ret;
}

void Mem_GIOWrite8(u32 addr, u8 val)
{
	asm("stmdb sp!, {r12}");
	
	iprintf("write8 @ $42%02X : %02x\n", addr, val);
	
	asm("ldmia sp!, {r12}");
}

void Mem_GIOWrite16(u32 addr, u16 val)
{
	asm("stmdb sp!, {r12}");
	
	iprintf("write16 @ $42%02X : %04x\n", addr, val);
	
	asm("ldmia sp!, {r12}");
}

