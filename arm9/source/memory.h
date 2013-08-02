
#ifndef _MEMORY_H_
#define _MEMORY_H_

extern u8 ROM_Bank0[0x8000];

extern bool Mem_HiROM;
extern u32 Mem_PtrTable[0x800] DTCM_BSS;
//extern u8 Mem_TimingTable[0x800] DTCM_BSS;

//extern void (*ROM_CacheCode)(u32 bank);
//extern void (*ROM_CacheData)(u32 bank);


void ROM_DoCacheBank(int bank);

bool Mem_LoadROM(char* path);
void Mem_Reset();

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

#endif
