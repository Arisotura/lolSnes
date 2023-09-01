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

#ifndef _MEMORY_H_
#define _MEMORY_H_

#include <stdio.h>
#include <fat.h>

#include "../../common/ipc.h"

#define ROMCACHE_SIZE 32

typedef struct
{
	u8 __pad3[2];		// -40 
	u8 LastBusVal;		// -38
	u8 __pad2;			// -37
	
	s32 SPC_CyclesPerLine;	// -36 | cycleratio * 1364
	s32 SPC_CycleRatio;		// -32
	s32 SPC_LastCycle;		// -28 | SPC cycle count (<<24) at last SPC run
	
	u16 IRQ_VMatch;		// -24
	u16 IRQ_HMatch;		// -22
	u16 IRQ_CurHMatch; 	// -20 | reset when the IRQ is fired
	
	u16 VCount;		// -18
	
	u8 __pad1[2];	// -16 for 'full' HCount (lower 16 bits are garbage)
	u16 HCount;		// -14
	
	u32 SRAMMask;	// -0xC
	
	u8 TotalLines;		// -8 | 262 for NTSC, 312 for PAL
	u8 ScreenHeight; 	// -7 | 224 or 239
	
	u8 IRQCond;		// -0x6
	
	// bit7: vblank
	// bit6: hblank
	// bit5: vblank (ack)
	// bit4: IRQ (ack)
	// bit3: IRQ (sticky, per-scanline)
	u8 HVBFlags;	// -0x5
	
	u32 SRAMDirty;	// -0x4
	
} Mem_StatusData;

#define MEMSTATUS_SIZE ((sizeof(Mem_StatusData) + 3) & ~3)

#define MEM_PTR(b, a) Mem_PtrTable[((b) << 3) | ((a) >> 13)]

#define MPTR_SLOW		(1 << 28)
#define MPTR_SPECIAL	(1 << 29)
#define MPTR_READONLY	(1 << 30)
#define MPTR_SRAM		(1 << 31)

extern u32 ROM_BaseOffset DTCM_BSS;
extern FILE* ROM_File DTCM_DATA;
extern u32 ROM_FileSize DTCM_BSS;

extern u8* ROM_Cache[3 + ROMCACHE_SIZE];
extern u8* ROM_Bank0;
extern u8* ROM_Bank0End;

extern u8 ROM_Region;

extern bool Mem_HiROM;
extern bool Mem_FastROM;
extern u32* Mem_PtrTable;
extern Mem_StatusData* Mem_Status;

extern u8 Mem_SysRAM[0x20000];

extern u16 Mem_VMatch;
extern u16 Mem_HMatch;
extern u16 Mem_HCheck;


//void ROM_DoCacheBank(u32 bank, u32 type);
void ROM_CacheBank(u32 bank, u32 type);
void ROM_SpeedChanged();

bool Mem_LoadROM(char* path);
void Mem_Reset();

void Mem_SaveSRAM();

ITCM_CODE u8 Mem_IORead8(u32 addr);
ITCM_CODE u16 Mem_IORead16(u32 addr);
ITCM_CODE void Mem_IOWrite8(u32 addr, u32 val);
ITCM_CODE void Mem_IOWrite16(u32 addr, u32 val);

ITCM_CODE u8 Mem_ROMRead8(u32 fileaddr);
ITCM_CODE u16 Mem_ROMRead16(u32 fileaddr);
ITCM_CODE u32 Mem_ROMRead24(u32 fileaddr);

ITCM_CODE void report_unk_lol(u32 op, u32 pc);
void reportBRK(u32 pc);

u8 Mem_GIORead8(u32 addr);
u16 Mem_GIORead16(u32 addr);
void Mem_GIOWrite8(u32 addr, u8 val);
void Mem_GIOWrite16(u32 addr, u16 val);

u8 Mem_JoyRead8(u32 addr);
u16 Mem_JoyRead16(u32 addr);
void Mem_JoyWrite8(u32 addr, u8 val);
void Mem_JoyWrite16(u32 addr, u16 val);

u8 DMA_Read8(u32 addr);
u16 DMA_Read16(u32 addr);
void DMA_Write8(u32 addr, u8 val);
void DMA_Write16(u32 addr, u16 val);
void DMA_Enable(u8 flag);

u8 Mem_Read8(u32 addr);
u16 Mem_Read16(u32 addr);
void Mem_Write8(u32 addr, u8 val);
void Mem_Write16(u32 addr, u8 val);

#endif
