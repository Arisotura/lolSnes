#include <nds.h>
#include <stdio.h>

#include "memory.h"

// PPU
//
// TODO LIST:
// * track individual palette modifications
// * track BG CHR modifications
// * everything regarding OBJ


#define BG_CHR_BASE		0x06000000
#define BG_SCR_BASE		0x06040000
#define OBJ_BASE		0x06400000
#define BG_PAL_BASE		0x06890000


u16 PPU_YOffset = 16;

u8 PPU_CGRAMAddr = 0;
u16 PPU_CurColor = 0xFFFF;
u8 PPU_CGRFlag = 0;

u8 PPU_VRAM[0x10000];
u16 PPU_VRAMAddr = 0;
u16 PPU_VRAMVal = 0;

// VRAM mapping table
// each entry applies to 1K of SNES VRAM
typedef struct
{
	u16 ChrUsage;		// bit0-3 -> BG0-BG3, bit4 -> OBJ
	u16 ScrUsage;		// same
	
} PPU_VRAMBlock;
PPU_VRAMBlock PPU_VRAMMap[64];

// 1024 tiles -> 16K SNES / 64K DS
// 1 BG -> 2 to 8 scr blocks
// max scr: 32k

// mode 0: 	4/4/4/4		64k/64k/64k/64k		256k
// mode 1:	16/16/4		64k/64k/64k			192k
// mode 2: 	16/16		64k/64k				128k
// mode 3: 	256/16		64k/64k				128k
// mode 4:	256/4		64k/64k				128k
// mode 5:	16/4		64k/64k				128k
// mode 6:	16			64k					64k
// mode 7:	256			64k					64k (may not be right)

// OBJ: max size = 64x64 -> 64 tiles
// max tiles: 512 -> 16k bytes
// (allocate more OBJ for some workarounds)

// VRAM ALLOCATION
// 0x06040000 : bank D: BG scr data
// 0x06000000 : bank A/B: BG chr data
// 0x06400000 : bank E: OBJ chr data
// bank F/G: BG ext palettes
// (bank C: console shiz)

typedef struct
{
	u16 ChrBase;
	u16 ChrSize;
	u16 ScrBase;
	u16 ScrSize;	// size of the scr data (2K/4K/8K)
	u16 MapSize;	// BG size (0-3)
	
	u16 __pad0;
	
	int ColorDepth;
	
	u16 ScrollX, ScrollY;

} PPU_Background;
PPU_Background PPU_BG[4];

u8 PPU_Mode;


void PPU_Reset()
{
	int i;
	
	PPU_CGRAMAddr = 0;
	PPU_CurColor = 0xFFFF;
	PPU_CGRFlag = 0;
	
	for (i = 0; i < 0x400; i += 4)
		*(u32*)(0x05000000 + i) = 0;
	
	PPU_VRAMAddr = 0;
	PPU_VRAMVal = 0;
	
	for (i = 0; i < 64; i++)
	{
		PPU_VRAMMap[i].ChrUsage = 0;
		PPU_VRAMMap[i].ScrUsage = 0;
	}
	for (i = 0; i < 8; i++)
		PPU_VRAMMap[i].ChrUsage = 0x1F;
	for (i = 8; i < 16; i++)
		PPU_VRAMMap[i].ChrUsage = 0x0F;
	PPU_VRAMMap[0].ScrUsage = 0x1F;
	PPU_VRAMMap[1].ScrUsage = 0x1F;
	
	for (i = 0; i < 0x10000; i += 4)
		*(u32*)&PPU_VRAM[i] = 0;
		
	for (i = 0; i < 4; i++)
	{
		PPU_Background* bg = &PPU_BG[i];
		bg->ChrBase = 0;
		bg->ScrBase = 0;
		bg->ColorDepth = 0;
		
		bg->ScrollX = 0;
		bg->ScrollY = 0;
	}
	
	
	PPU_Mode = 0;
		
	
	// allocate VRAM
	*(u8*)0x04000240 = 0x81;
	*(u8*)0x04000241 = 0x89;
	*(u8*)0x04000243 = 0x91;
	*(u8*)0x04000244 = 0x82;
	*(u8*)0x04000245 = 0x84;
	*(u8*)0x04000246 = 0x8C;
	
	// setup BGs
	*(u32*)0x04000000 = 0x40010F10 | (4 << 27);
	*(u16*)0x04000008 = 0x0080 | (0 << 4) | (0 << 10);
	*(u16*)0x0400000A = 0x0080 | (1 << 4) | (1 << 10);
	*(u16*)0x0400000C = 0x0080 | (2 << 4) | (2 << 10);
	*(u16*)0x0400000E = 0x0080 | (3 << 4) | (3 << 10);
}


void PPU_UploadBGPal(int nbg)
{
	PPU_Background* bg = &PPU_BG[nbg];
	
	if (nbg < 2)
		*(u8*)0x04000245 = 0x80;
	else
		*(u8*)0x04000246 = 0x80;
	
	u16* src = (u16*)0x05000000;
	u16* dst = (u16*)(BG_PAL_BASE + (nbg << 13));
	
	if (bg->ColorDepth == 4)
	{
		int i;
		for (i = 0; i < 8; i++)
		{
			*dst++ = *src++;
			*dst++ = *src++;
			*dst++ = *src++;
			*dst++ = *src++;
			
			dst += 252;
		}
	}
	else if (bg->ColorDepth == 16)
	{
		int i;
		for (i = 0; i < 8; i++)
		{
			*dst++ = *src++;
			*dst++ = *src++;
			*dst++ = *src++;
			*dst++ = *src++;
			*dst++ = *src++;
			*dst++ = *src++;
			*dst++ = *src++;
			*dst++ = *src++;
			*dst++ = *src++;
			*dst++ = *src++;
			*dst++ = *src++;
			*dst++ = *src++;
			*dst++ = *src++;
			*dst++ = *src++;
			*dst++ = *src++;
			*dst++ = *src++;
			
			dst += 240;
		}
	}
	else
	{
		// TODO
	}
	
	if (nbg < 2)
		*(u8*)0x04000245 = 0x84;
	else
		*(u8*)0x04000246 = 0x8C;
}

void PPU_UploadBGChr(int nbg)
{
	PPU_Background* bg = &PPU_BG[nbg];
	u16 chrbase = bg->ChrBase;
	
	if (bg->ColorDepth == 0)
		return;
	
	if (bg->ColorDepth == 4)
	{
		u8* bp12 = &PPU_VRAM[chrbase];
		u16* dst = (u16*)(BG_CHR_BASE + (nbg << 16));
		
		int t;
		for (t = 0; t < 1024; t++)
		{
			int y;
			for (y = 0; y < 8; y++)
			{
				u8 	b1 = *bp12++,
					b2 = *bp12++;

				*dst++ 	= ((b1 & 0x80) >> 7) | ((b2 & 0x80) >> 6)
						| ((b1 & 0x40) << 2) | ((b2 & 0x40) << 3);
				*dst++ 	= ((b1 & 0x20) >> 5) | ((b2 & 0x20) >> 4)
						| ((b1 & 0x10) << 4) | ((b2 & 0x10) << 5);
				*dst++ 	= ((b1 & 0x08) >> 3) | ((b2 & 0x08) >> 2)
						| ((b1 & 0x04) << 6) | ((b2 & 0x04) << 7);
				*dst++ 	= ((b1 & 0x02) >> 1) | (b2 & 0x02)
						| ((b1 & 0x01) << 8) | ((b2 & 0x01) << 9);
			}
		}
	}
	else if (bg->ColorDepth == 16)
	{
		u8* bp12 = &PPU_VRAM[chrbase];
		u8* bp34 = bp12 + 16;
		u16* dst = (u16*)(BG_CHR_BASE + (nbg << 15));
		
		int t;
		for (t = 0; t < 1024; t++)
		{
			int y;
			for (y = 0; y < 8; y++)
			{
				u8 	b1 = *bp12++,
					b2 = *bp12++,
					b3 = *bp34++,
					b4 = *bp34++;

				*dst++ 	= ((b1 & 0x80) >> 7) | ((b2 & 0x80) >> 6) | ((b3 & 0x80) >> 5) | ((b4 & 0x80) >> 4)
						| ((b1 & 0x40) << 2) | ((b2 & 0x40) << 3) | ((b3 & 0x40) << 4) | ((b4 & 0x40) << 5);
				*dst++ 	= ((b1 & 0x20) >> 5) | ((b2 & 0x20) >> 4) | ((b3 & 0x20) >> 3) | ((b4 & 0x20) >> 2)
						| ((b1 & 0x10) << 4) | ((b2 & 0x10) << 5) | ((b3 & 0x10) << 6) | ((b4 & 0x10) << 7);
				*dst++ 	= ((b1 & 0x08) >> 3) | ((b2 & 0x08) >> 2) | ((b3 & 0x08) >> 1) | (b4 & 0x08)
						| ((b1 & 0x04) << 6) | ((b2 & 0x04) << 7) | ((b3 & 0x04) << 8) | ((b4 & 0x04) << 9);
				*dst++ 	= ((b1 & 0x02) >> 1) | (b2 & 0x02) | ((b3 & 0x02) << 1) | ((b4 & 0x02) << 2)
						| ((b1 & 0x01) << 8) | ((b2 & 0x01) << 9) | ((b3 & 0x01) << 10) | ((b4 & 0x01) << 11);
			}
			
			bp12 += 16;
			bp34 += 16;
		}
	}
	else
		iprintf("Unsupported color depth %d for BG%d\n", bg->ColorDepth, nbg);
}

void PPU_UploadBGScr(int nbg)
{
	PPU_Background* bg = &PPU_BG[nbg];
	
	if (bg->ColorDepth == 0)
		return;
	
	int bgsize = bg->ScrSize;
	u16* src = (u16*)&PPU_VRAM[bg->ScrBase];
	u16* dst = (u16*)(BG_SCR_BASE + (nbg << 13));
	
	int t;
	for (t = 0; t < bgsize; t++)
	{
		u16 stile = *src++;
		u16 dtile = stile & 0x03FF;
		
		if (stile & 0x2000)
		{
			// tile with priority
			// TODO
		}
		
		if (stile & 0x4000)
			dtile |= 0x0400;
		if (stile & 0x8000)
			dtile |= 0x0800;
		
		dtile |= (stile & 0x1C00) << 2;
		*dst++ = dtile;
	}
}

inline void PPU_SetBGColorDepth(int nbg, int depth)
{
	PPU_Background* bg = &PPU_BG[nbg];
	
	if (bg->ColorDepth != depth)
	{
		bg->ColorDepth = depth;
		
		if (depth != 0)
		{
			register int bmask = (1 << nbg);
			int i, size;
			
			switch (depth)
			{
				case 4: bg->ChrSize = 0x4000; break;
				case 16: bg->ChrSize = 0x8000; break;
				case 256: bg->ChrSize = 0x10000; break;
			}
			
			size = bg->ChrSize >> 10;
			for (i = 0; i < size; i++)
				PPU_VRAMMap[(bg->ChrBase >> 10) + i].ChrUsage |= bmask;
			
			PPU_UploadBGPal(nbg);
			PPU_UploadBGChr(nbg);
			// no need to update scr since it's independent from color depth
		}
		else
			bg->ChrSize = 0;
	}
}

void PPU_ModeChange(u8 newmode)
{
	int i;
	
	if (newmode == PPU_Mode) return;
	PPU_Mode = newmode;
	
	for (i = 0; i < 64; i++)
		PPU_VRAMMap[i].ChrUsage &= 0xF0;
	
	switch (newmode)
	{
		case 1:
			PPU_SetBGColorDepth(0, 16);
			PPU_SetBGColorDepth(1, 16);
			PPU_SetBGColorDepth(2, 4);
			PPU_SetBGColorDepth(3, 0);
			break;
		
		default:
			iprintf("unsupported PPU mode %d\n", newmode);
			break;
	}
}


inline void PPU_SetXScroll(int nbg, u8 val)
{
	PPU_Background* bg = &PPU_BG[nbg];
	
	if (!(bg->ScrollX & 0x8000))
		bg->ScrollX = val | 0x8000;
	else
	{
		bg->ScrollX &= 0xFF;
		bg->ScrollX |= (val & 0x1F);
		*(u16*)(0x04000010 + (nbg<<2)) = bg->ScrollX;
	}
}

inline void PPU_SetYScroll(int nbg, u8 val)
{
	PPU_Background* bg = &PPU_BG[nbg];
	
	if (!(bg->ScrollY & 0x8000))
		bg->ScrollY = val | 0x8000;
	else
	{
		bg->ScrollY &= 0xFF;
		bg->ScrollY |= (val & 0x1F);
		*(u16*)(0x04000012 + (nbg<<2)) = bg->ScrollY + PPU_YOffset;
	}
}

inline void PPU_SetBGSCR(int nbg, u8 val)
{
	PPU_Background* bg = &PPU_BG[nbg];
	
	int oldscrbase = bg->ScrBase;
	int oldsize = bg->ScrSize >> 10;
	
	bg->ScrBase = (val & 0xFC) << 9;
	bg->MapSize = val & 0x03;
	switch (val & 0x03)
	{
		case 0x00: bg->ScrSize = 2048; break;
		case 0x01:
		case 0x02: bg->ScrSize = 4096; break;
		case 0x03: bg->ScrSize = 8192; break;
	}
	
	if (bg->ScrBase != oldscrbase)
	{
		register int bmask = (1 << nbg), nbmask = ~bmask;
		
		int i, size = bg->ScrSize >> 10;
		for (i = 0; i < oldsize; i++)
			PPU_VRAMMap[(oldscrbase >> 10) + i].ScrUsage &= nbmask;
		for (i = 0; i < size; i++)
			PPU_VRAMMap[(bg->ScrBase >> 10) + i].ScrUsage |= bmask;
		
		PPU_UploadBGScr(nbg);
	}
}

inline void PPU_SetBGCHR(int nbg, u8 val)
{
	PPU_Background* bg = &PPU_BG[nbg];
	
	int oldchrbase = bg->ChrBase;
	bg->ChrBase = val << 13;
	
	if (bg->ChrBase != oldchrbase)
	{
		register int bmask = (1 << nbg), nbmask = ~bmask;
		
		int i, size = bg->ChrSize >> 10;
		for (i = 0; i < size; i++)
		{
			PPU_VRAMMap[(oldchrbase >> 10) + i].ChrUsage &= nbmask;
			PPU_VRAMMap[(bg->ChrBase >> 10) + i].ChrUsage |= bmask;
		}
		
		PPU_UploadBGChr(nbg);
	}
}

inline void PPU_UpdateVRAM_CHR(int nbg, u32 addr, u16 val)
{
	// TODO
}

inline void PPU_UpdateVRAM_SCR(int nbg, u32 addr, u16 stile)
{
	u16 dtile = stile & 0x03FF;
		
	if (stile & 0x2000)
	{
		// tile with priority
		// TODO
	}
	
	if (stile & 0x4000)
		dtile |= 0x0400;
	if (stile & 0x8000)
		dtile |= 0x0800;
	
	dtile |= (stile & 0x1C00) << 2;
	
	*(u16*)(BG_SCR_BASE + (nbg << 13) + addr - PPU_BG[nbg].ScrBase) = dtile;
}

inline void PPU_UpdateVRAM(u32 addr, u16 val)
{
	PPU_VRAMBlock* block = &PPU_VRAMMap[addr >> 10];
	
	if (block->ChrUsage & 0x0001) PPU_UpdateVRAM_CHR(0, addr, val);
	if (block->ChrUsage & 0x0002) PPU_UpdateVRAM_CHR(1, addr, val);
	if (block->ChrUsage & 0x0004) PPU_UpdateVRAM_CHR(2, addr, val);
	if (block->ChrUsage & 0x0008) PPU_UpdateVRAM_CHR(3, addr, val);
	
	if (block->ScrUsage & 0x0001) PPU_UpdateVRAM_SCR(0, addr, val);
	if (block->ScrUsage & 0x0002) PPU_UpdateVRAM_SCR(1, addr, val);
	if (block->ScrUsage & 0x0004) PPU_UpdateVRAM_SCR(2, addr, val);
	if (block->ScrUsage & 0x0008) PPU_UpdateVRAM_SCR(3, addr, val);
	
	// TODO OBJ
}


// I/O
// addr = lowest 8 bits of address in $21xx range

u8 PPU_Read8(u32 addr)
{
	asm("stmdb sp!, {r12}");
	
	u8 ret = 0;
	switch (addr)
	{
		case 0x21:
			ret = PPU_CGRAMAddr;
			break;
		
		case 0x22:
		{
			u8 val = *(u8*)(0x05000000 + (PPU_CGRAMAddr << 1) + PPU_CGRFlag);
			if (PPU_CGRFlag) PPU_CGRAMAddr++;
			PPU_CGRFlag = !PPU_CGRFlag;
			ret = val;
		}
		break;
		
		case 0x40: ret = SPC_IOPorts[4]; break;
		case 0x41: ret = SPC_IOPorts[5]; break;
		case 0x42: ret = SPC_IOPorts[6]; break;
		case 0x43: ret = SPC_IOPorts[7]; break;
	}
	
	asm("ldmia sp!, {r12}");
	return ret;
}

u16 PPU_Read16(u32 addr)
{
	asm("stmdb sp!, {r12}");
	
	u16 ret = 0;
	switch (addr)
	{
		// not in the right place, but well
		// our I/O functions are mapped to the whole $21xx range
		
		case 0x40: ret = *(u16*)&SPC_IOPorts[4]; break;
		case 0x42: ret = *(u16*)&SPC_IOPorts[6]; break;
	}
	
	asm("ldmia sp!, {r12}");
	return ret;
}

void PPU_Write8(u32 addr, u8 val)
{
	asm("stmdb sp!, {r12}");
	
	switch (addr)
	{
		case 0x05:
			if (val & 0xF0) iprintf("!! 16x16 TILES NOT SUPPORTED\n");
			// TODO prio (bit 3)
			PPU_ModeChange(val & 0x07);
			break;
			
			
		case 0x07: PPU_SetBGSCR(0, val); break;
		case 0x08: PPU_SetBGSCR(1, val); break;
		case 0x09: PPU_SetBGSCR(2, val); break;
		case 0x0A: PPU_SetBGSCR(3, val); break;
			
			
		case 0x0B:
			PPU_SetBGCHR(0, val & 0x0F);
			PPU_SetBGCHR(1, val >> 4);
			break;
		case 0x0C:
			PPU_SetBGCHR(2, val & 0x0F);
			PPU_SetBGCHR(3, val >> 4);
			break;
			
		
		case 0x0D: PPU_SetXScroll(0, val); break;
		case 0x0E: PPU_SetYScroll(0, val); break;
		case 0x0F: PPU_SetXScroll(1, val); break;
		case 0x10: PPU_SetYScroll(1, val); break;
		case 0x11: PPU_SetXScroll(2, val); break;
		case 0x12: PPU_SetYScroll(2, val); break;
		case 0x13: PPU_SetXScroll(3, val); break;
		case 0x14: PPU_SetYScroll(3, val); break;
			
		
		case 0x15:
			if (val != 0x80) iprintf("UNSUPPORTED VRAM MODE %02X\n", val);
			printf("vram = %08X\n", (u32)&PPU_VRAM);
			break;
			
		case 0x16:
			PPU_VRAMAddr &= 0xFF00;
			PPU_VRAMAddr |= val;
			break;
		case 0x17:
			PPU_VRAMAddr &= 0x00FF;
			PPU_VRAMAddr |= (u16)val << 8;
			break;
		
		case 0x18: // VRAM shit
			PPU_VRAMVal &= 0xFF00;
			PPU_VRAMVal |= val;
			break;
		case 0x19:
			PPU_VRAMVal &= 0x00FF;
			PPU_VRAMVal |= (u16)val << 8;
			*(u16*)&PPU_VRAM[PPU_VRAMAddr << 1] = PPU_VRAMVal;
			PPU_UpdateVRAM(PPU_VRAMAddr << 1, PPU_VRAMVal);
			PPU_VRAMVal = 0;
			PPU_VRAMAddr++;
			// TODO: support other increment modes
			break;
			
		case 0x21:
			PPU_CGRAMAddr = val;
			PPU_CGRFlag = 0;
			break;
		
		case 0x22:
			if (!PPU_CGRFlag)
			{
				PPU_CurColor = (u16)val;
			}
			else
			{
				PPU_CurColor |= (val << 8);
				register u16 paddr = PPU_CGRAMAddr << 1;
				*(u16*)(0x05000000 + paddr) = PPU_CurColor;
				*(u16*)(0x05000200 + paddr) = PPU_CurColor;
				// TODO propagate palette changes to ext palettes
				PPU_CGRAMAddr++;
			}
			PPU_CGRFlag = !PPU_CGRFlag;
			break;
		
		case 0x2C:
			*(u32*)0x04000000 &= 0xFFFFE0FF;
			*(u32*)0x04000000 |= ((val & 0x1F) << 8);
			break;
		case 0x2D:
			iprintf("21%02X = %02X\n", addr, val);
			break;
			
		case 0x40: SPC_IOPorts[0] = val; break;
		case 0x41: SPC_IOPorts[1] = val; break;
		case 0x42: SPC_IOPorts[2] = val; break;
		case 0x43: SPC_IOPorts[3] = val; break;
		
		default:
			//iprintf("PPU_Write8(%08X, %08X)\n", addr, val);
			break;
	}
	
	asm("ldmia sp!, {r12}");
}

void PPU_Write16(u32 addr, u16 val)
{
	asm("stmdb sp!, {r12}");
	
	switch (addr)
	{
		// optimized route
		
		case 0x16:
			PPU_VRAMAddr = val;
			break;
			
		case 0x40: *(u16*)&SPC_IOPorts[0] = val; break;
		case 0x42: *(u16*)&SPC_IOPorts[2] = val; break;
		
		case 0x41:
		case 0x43: iprintf("!! write $21%02X %04X\n", addr, val); break;
		
		// otherwise, just do two 8bit writes
		default:
			PPU_Write8(addr, val & 0x00FF);
			PPU_Write8(addr+1, val >> 8);
			break;
	}
	
	asm("ldmia sp!, {r12}");
}
