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

#include "../../common/ipc.h"

#define ROMCACHE_SIZE 32

typedef struct
{
	u8 __pad0[3];
	u8 HVBFlags;	// bit7: vblank | bit6: hblank | bit5: vblank (ack)
	
	u32 SRAMDirty;
	
} Mem_StatusData;

#define MEMSTATUS_SIZE ((sizeof(Mem_StatusData) + 3) & ~3)

extern u8* ROM_Cache[3 + ROMCACHE_SIZE];
extern u8* ROM_Bank0;
extern u8* ROM_Bank0End;

extern bool Mem_HiROM;
extern u32* Mem_PtrTable;

extern u8 Mem_SysRAM[0x20000];

extern u16 Mem_VMatch;


void ROM_DoCacheBank(int bank, int type);

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
