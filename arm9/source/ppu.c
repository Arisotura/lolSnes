#include <nds.h>
#include <stdio.h>

#include "memory.h"

// PPU
//
// TODO LIST:
// * track individual palette modifications
// * track BG CHR modifications
// * everything regarding OBJ
// * (take SPC stuff out of the PPU?)


#define BG_CHR_BASE		0x06000000
#define BG_SCR_BASE		0x06040000
#define OBJ_BASE		0x06400000
#define BG_PAL_BASE		0x06890000


u16 PPU_YOffset = 16;

u8 PPU_CGRAMAddr = 0;
u16 PPU_CurColor = 0xFFFF;
u8 PPU_CGRFlag = 0;
u8 PPU_CGRDirty = 0;

u8 PPU_VRAM[0x10000];
u16 PPU_VRAMAddr = 0;
u16 PPU_VRAMVal = 0;
u8 PPU_VRAMInc = 0;
u8 PPU_VRAMStep = 0;
u16 PPU_VRAMPref = 0;

u8 PPU_OAM[0x220];
u16 PPU_OAMAddr = 0;
u16 PPU_OAMVal = 0;
u8 PPU_OAMPrio = 0;
u8 PPU_FirstOAM = 0;
u8 PPU_OAMDirty = 0;

// VRAM mapping table
// each entry applies to 2K of SNES VRAM
typedef struct
{
	u16 ChrUsage;		// bit0-3 -> BG0-BG3, bit4 -> OBJ
	u16 ScrUsage;		// same
	
} PPU_VRAMBlock;
PPU_VRAMBlock PPU_VRAMMap[32];

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
// bank C: ARM7 VRAM
// bank H: bottom screen

typedef struct
{
	u16 ChrBase;
	u16 ScrBase;
	u32 ChrSize;
	u16 ScrSize;	// size of the scr data (2K/4K/8K)
	u16 MapSize;	// BG size (0-3)
	
	int ColorDepth;
	
	u16 ScrollX, ScrollY;

} PPU_Background;
PPU_Background PPU_BG[4];

u8 PPU_Mode;

u8 PPU_BGMain, PPU_BGSub;

u8 PPU_OBJSize;
u16 PPU_OBJBase;
u16 PPU_OBJGap;

/*
         Val Small  Large
         0 = 8x8    16x16    ;Caution:
         1 = 8x8    32x32    ;In 224-lines mode, OBJs with 64-pixel height
         2 = 8x8    64x64    ;may wrap from lower to upper screen border.
         3 = 16x16  32x32    ;In 239-lines mode, the same problem applies
         4 = 16x16  64x64    ;also for OBJs with 32-pixel height.
         5 = 32x32  64x64
         6 = 16x32  32x64 (undocumented)
         7 = 16x32  32x32 (undocumented)
*/
u8 _PPU_OBJSizes[16] = 
{
	0, 1,
	0, 2,
	0, 3,
	1, 2,
	1, 3,
	2, 3,
	0x82, 0x83,
	0x82, 2
};
u8* PPU_OBJSizes;

/*u8 _PPU_OBJPrio[32] = 
{
	2, 0x82, 0, 0x80,
	2, 0x82, 0, 0x80,
	1, 0, 0x80, 0x80,
	1, 0, 0, 0,
	1, 0, 0, 0,
	1, 0, 0, 0,
	1, 0, 0, 0,
	1, 0, 0, 0
};
u8* PPU_OBJPrio;*/

u16 PPU_OBJList[4*128];

u8 PPU_SpriteSize[16] =
{
	8, 16, 32, 64,
	8, 8, 16, 32,
	8, 16, 32, 64,
	16, 32, 32, 64
};


void PPU_Reset()
{
	int i;
	
	PPU_CGRAMAddr = 0;
	PPU_CurColor = 0xFFFF;
	PPU_CGRFlag = 0;
	PPU_CGRDirty = 0;
	
	for (i = 0; i < 0x400; i += 4)
		*(u32*)(0x05000000 + i) = 0;
	for (i = 0; i < 0x400; i += 4)
		*(u32*)(0x07000000 + i) = 0;
	
	PPU_VRAMAddr = 0;
	PPU_VRAMVal = 0;
	PPU_VRAMPref = 0;
	
	PPU_OAMAddr = 0;
	PPU_OAMVal = 0;
	PPU_OAMPrio = 0;
	PPU_FirstOAM = 0;
	PPU_OAMDirty = 0;
	
	for (i = 0; i < 32; i++)
	{
		PPU_VRAMMap[i].ChrUsage = 0;
		PPU_VRAMMap[i].ScrUsage = 0;
	}
	/*for (i = 0; i < 4; i++)
		PPU_VRAMMap[i].ChrUsage = 0x1F;
	for (i = 8; i < 8; i++)
		PPU_VRAMMap[i].ChrUsage = 0x0F;
	PPU_VRAMMap[0].ScrUsage = 0x1F;*/
	
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
	
	PPU_BGMain = 0;
	PPU_BGSub = 0;
	
	
	PPU_OBJSize = 0;
	PPU_OBJBase = 0;
	PPU_OBJGap = 0;
	PPU_OBJSizes = _PPU_OBJSizes;
	
	for (i = 0; i < 4*128; i++)
		PPU_OBJList[i] = 0;
		
	
	// allocate VRAM
	*(u8*)0x04000240 = 0x81;
	*(u8*)0x04000241 = 0x89;
	*(u8*)0x04000243 = 0x91;
	*(u8*)0x04000244 = 0x82;
	*(u8*)0x04000245 = 0x84;
	*(u8*)0x04000246 = 0x8C;
	
	// setup BGs
	*(u32*)0x04000000 = 0x40010000 | (4 << 27);
	*(u16*)0x04000008 = 0x0080 | (0 << 4) | (0 << 10);
	*(u16*)0x0400000A = 0x0081 | (1 << 4) | (1 << 10);
	*(u16*)0x0400000C = 0x0082 | (2 << 4) | (2 << 10);
	*(u16*)0x0400000E = 0x0083 | (3 << 4) | (3 << 10);
	
	
	printf("vram = %08X | %08X\n", (u32)&PPU_VRAM, (u32)&PPU_VRAMMap);
}


void PPU_UploadBGPal(int nbg, bool domap)
{
	PPU_Background* bg = &PPU_BG[nbg];
	
	if (domap)
	{
		if (nbg < 2)
			*(u8*)0x04000245 = 0x80;
		else
			*(u8*)0x04000246 = 0x80;
	}
	
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
	
	if (domap)
	{
		if (nbg < 2)
			*(u8*)0x04000245 = 0x84;
		else
			*(u8*)0x04000246 = 0x8C;
	}
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
		u16* dst = (u16*)(BG_CHR_BASE + (nbg << 16));
		
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

void PPU_UploadOBJChr()
{
	u16 chrbase = PPU_OBJBase;
	
	u8* bp12 = &PPU_VRAM[chrbase];
	u8* bp34 = bp12 + 16;
	u16* dst = (u16*)OBJ_BASE;
	
	int t;
	for (t = 0; t < 512; t++)
	{
		int y;
		for (y = 0; y < 8; y++)
		{
			u8 	b1 = *bp12++,
				b2 = *bp12++,
				b3 = *bp34++,
				b4 = *bp34++;

			*dst++ 	= ((b1 & 0x80) >> 7) | ((b2 & 0x80) >> 6) | ((b3 & 0x80) >> 5) | ((b4 & 0x80) >> 4)
					| ((b1 & 0x40) >> 2) | ((b2 & 0x40) >> 1) | ((b3 & 0x40)) | ((b4 & 0x40) << 1)
					| ((b1 & 0x20) << 3) | ((b2 & 0x20) << 4) | ((b3 & 0x20) << 5) | ((b4 & 0x20) << 6)
					| ((b1 & 0x10) << 8) | ((b2 & 0x10) << 9) | ((b3 & 0x10) << 10) | ((b4 & 0x10) << 11);
			*dst++ 	= ((b1 & 0x08) >> 3) | ((b2 & 0x08) >> 2) | ((b3 & 0x08) >> 1) | (b4 & 0x08)
					| ((b1 & 0x04) << 2) | ((b2 & 0x04) << 3) | ((b3 & 0x04) << 4) | ((b4 & 0x04) << 5)
					| ((b1 & 0x02) << 7) | ((b2 & 0x02) << 8) | ((b3 & 0x02) << 9) | ((b4 & 0x02) << 10)
					| ((b1 & 0x01) << 12) | ((b2 & 0x01) << 13) | ((b3 & 0x01) << 14) | ((b4 & 0x01) << 15);
		}
		
		bp12 += 16;
		bp34 += 16;
		
		if ((t & 0xF) == 0xF)
			dst += 256;
	}
}

inline void PPU_SetBGColorDepth(int nbg, int depth)
{
	PPU_Background* bg = &PPU_BG[nbg];
	
	if (bg->ColorDepth != depth)
	{
		int olddepth = bg->ColorDepth;
		bg->ColorDepth = depth;
		
		if (depth != 0)
		{
			register int bmask = (1 << nbg), nbmask = ~bmask;
			int i, size;
			
			int oldsize = bg->ChrSize >> 11;
			switch (depth)
			{
				case 4: bg->ChrSize = 0x4000; break;
				case 16: bg->ChrSize = 0x8000; break;
				case 256: bg->ChrSize = 0x10000; break;
			}
			
			size = bg->ChrSize >> 11;
			if (size != oldsize)
			{
				for (i = 0; i < oldsize; i++)
					if (((bg->ChrBase >> 11) + i) < 32)
						PPU_VRAMMap[(bg->ChrBase >> 11) + i].ChrUsage &= nbmask;
				for (i = 0; i < size; i++)
					if (((bg->ChrBase >> 11) + i) < 32)
						PPU_VRAMMap[(bg->ChrBase >> 11) + i].ChrUsage |= bmask;
			}
			
			PPU_UploadBGPal(nbg, true);
			PPU_UploadBGChr(nbg);
			if (olddepth == 0) PPU_UploadBGScr(nbg);
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
	
	for (i = 0; i < 32; i++)
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
			//iprintf("unsupported PPU mode %d\n", newmode);
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
		bg->ScrollX |= ((val & 0x1F) << 8);
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
		bg->ScrollY |= ((val & 0x1F) << 8);
		*(u16*)(0x04000012 + (nbg<<2)) = bg->ScrollY + PPU_YOffset;
	}
}

inline void PPU_SetBGSCR(int nbg, u8 val)
{
	PPU_Background* bg = &PPU_BG[nbg];
	
	int oldscrbase = bg->ScrBase;
	int oldsize = bg->ScrSize >> 11;
	
	bg->ScrBase = (val & 0xFC) << 9;
	bg->MapSize = val & 0x03;
	switch (val & 0x03)
	{
		case 0x00: bg->ScrSize = 2048; break;
		case 0x01:
		case 0x02: bg->ScrSize = 4096; break;
		case 0x03: bg->ScrSize = 8192; break;
	}
	
	if (bg->ScrBase != oldscrbase || (bg->ScrSize >> 11) != oldsize)
	{
		register int bmask = (1 << nbg), nbmask = ~bmask;
		
		int i, size = bg->ScrSize >> 11;
		for (i = 0; i < oldsize; i++)
			if (((oldscrbase >> 11) + i) < 32)
				PPU_VRAMMap[(oldscrbase >> 11) + i].ScrUsage &= nbmask;
		for (i = 0; i < size; i++)
			if (((bg->ScrBase >> 11) + i) < 32)
				PPU_VRAMMap[(bg->ScrBase >> 11) + i].ScrUsage |= bmask;
		
		PPU_UploadBGScr(nbg);
	}
	
	u16* bgctrl = (u16*)(0x04000008 + (nbg << 1));
	*bgctrl = (*bgctrl & 0xFFFF3FFF) | ((val & 0x03) << 14);
}

inline void PPU_SetBGCHR(int nbg, u8 val)
{
	PPU_Background* bg = &PPU_BG[nbg];
	
	int oldchrbase = bg->ChrBase;
	bg->ChrBase = val << 13;
	
	if (bg->ChrBase != oldchrbase)
	{
		register int bmask = (1 << nbg), nbmask = ~bmask;
		
		int i, size = bg->ChrSize >> 11;
		for (i = 0; i < size; i++)
		{
			register int oldi = (oldchrbase >> 11) + i;
			register int newi = (bg->ChrBase >> 11) + i;
			
			if (oldi < 32) PPU_VRAMMap[oldi].ChrUsage &= nbmask;
			if (newi < 32) PPU_VRAMMap[newi].ChrUsage |= bmask;
		}
		
		PPU_UploadBGChr(nbg);
	}
}

inline void PPU_SetOBJCHR(u16 base, u16 gap)
{
	if (base != PPU_OBJBase || gap != PPU_OBJGap)
	{
		int i;
		for (i = 0; i < 0x8; i++)
		{
			register int oldi = (PPU_OBJBase >> 11) + i;
			register int newi = (base >> 11) + i;
			
			if (i >= 0x4)
			{
				oldi += (PPU_OBJGap >> 11);
				newi += (gap >> 11);
			}
			
			if (oldi < 32) PPU_VRAMMap[oldi].ChrUsage &= 0xFFEF;
			if (newi < 32) PPU_VRAMMap[newi].ChrUsage |= 0x0010;
		}
		
		PPU_OBJBase = base;
		PPU_OBJGap = gap;
		
		PPU_UploadOBJChr();
	}
}

inline void PPU_UpdateVRAM_CHR(int nbg, u32 addr, u16 val)
{
	PPU_Background* bg = &PPU_BG[nbg];
	addr -= bg->ChrBase;
	u32 vramptr = BG_CHR_BASE + (nbg << 16);
	
	if (bg->ColorDepth == 4)
	{
		vramptr += (addr & 0xFFFFFFF0) << 2;
		vramptr += (addr & 0xE) << 2;
		
		*(u16*)vramptr = ((val & 0x0040) << 2) | ((val & 0x4000) >> 5) | 
								((val & 0x0080) >> 7) | ((val & 0x8000) >> 14);
		*(u16*)(vramptr + 2) = ((val & 0x0010) << 4) | ((val & 0x1000) >> 3) | 
								((val & 0x0020) >> 5) | ((val & 0x2000) >> 12);
		*(u16*)(vramptr + 4) = ((val & 0x0004) << 6) | ((val & 0x0400) >> 1) | 
								((val & 0x0008) >> 3) | ((val & 0x0800) >> 10);
		*(u16*)(vramptr + 6) = ((val & 0x0001) << 8) | ((val & 0x0100) << 1) | 
								((val & 0x0002) >> 1) | ((val & 0x0200) >> 8);
	}
	else if (bg->ColorDepth == 16)
	{
		vramptr += (addr & 0xFFFFFFE0) << 1;
		vramptr += (addr & 0xE) << 2;
		
		if (addr & 0x10)
		{
			*(u16*)vramptr = ((*(u16*)vramptr) & 0xF3F3) | ((val & 0x0040) << 4) | 
							((val & 0x4000) >> 3) | ((val & 0x0080) >> 5) | ((val & 0x8000) >> 12);
			*(u16*)(vramptr + 2) = ((*(u16*)(vramptr + 2)) & 0xF3F3) | ((val & 0x0010) << 6) | 
							((val & 0x1000) >> 1) | ((val & 0x0020) >> 3) | ((val & 0x2000) >> 10);
			*(u16*)(vramptr + 4) = ((*(u16*)(vramptr + 4)) & 0xF3F3) | ((val & 0x0004) << 8) | 
							((val & 0x0400) << 1) | ((val & 0x0008) >> 1) | ((val & 0x0800) >> 8);
			*(u16*)(vramptr + 6) = ((*(u16*)(vramptr + 6)) & 0xF3F3) | ((val & 0x0001) << 10) | 
							((val & 0x0100) << 3) | ((val & 0x0002) << 1) | ((val & 0x0200) >> 6);
		}
		else
		{
			*(u16*)vramptr = ((*(u16*)vramptr) & 0xFCFC) | ((val & 0x0040) << 2) | ((val & 0x4000) >> 5) | 
							((val & 0x0080) >> 7) | ((val & 0x8000) >> 14);
			*(u16*)(vramptr + 2) = ((*(u16*)(vramptr + 2)) & 0xFCFC) | ((val & 0x0010) << 4) | ((val & 0x1000) >> 3) | 
							((val & 0x0020) >> 5) | ((val & 0x2000) >> 12);
			*(u16*)(vramptr + 4) = ((*(u16*)(vramptr + 4)) & 0xFCFC) | ((val & 0x0004) << 6) | ((val & 0x0400) >> 1) | 
							((val & 0x0008) >> 3) | ((val & 0x0800) >> 10);
			*(u16*)(vramptr + 6) = ((*(u16*)(vramptr + 6)) & 0xFCFC) | ((val & 0x0001) << 8) | ((val & 0x0100) << 1) | 
							((val & 0x0002) >> 1) | ((val & 0x0200) >> 8);
		}
	}
	else
	{
		// TODO
	}
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

inline void PPU_UpdateVRAM_OBJ(u32 addr, u16 val)
{
	addr -= PPU_OBJBase;
	if (addr >= 8192) addr -= PPU_OBJGap;
	u32 vramptr = OBJ_BASE;

	//vramptr += addr & 0xFFFFFFE0;
	vramptr += (addr & 0xFE00) << 1;
	vramptr += addr & 0x1E0;
	vramptr += (addr & 0xE) << 1;
	
	if (addr & 0x10)
	{
		*(u16*)vramptr = ((*(u16*)vramptr) & 0x3333) | 
			((val & 0x0080) >> 5) | ((val & 0x8000) >> 12) | 
			((val & 0x0040)) | ((val & 0x4000) >> 7) |
			((val & 0x0020) << 5) | ((val & 0x2000) >> 2) |
			((val & 0x0010) << 10) | ((val & 0x1000) << 3);
		*(u16*)(vramptr + 2) = ((*(u16*)(vramptr + 2)) & 0x3333) | 
			((val & 0x0008) >> 1) | ((val & 0x0800) >> 8) | 
			((val & 0x0004) << 4) | ((val & 0x0400) >> 3) |
			((val & 0x0002) << 9) | ((val & 0x0200) << 2) |
			((val & 0x0001) << 14) | ((val & 0x0100) << 7);
	}
	else
	{
		*(u16*)vramptr = ((*(u16*)vramptr) & 0xCCCC) | 
			((val & 0x0080) >> 7) | ((val & 0x8000) >> 14) | 
			((val & 0x0040) >> 2) | ((val & 0x4000) >> 9) |
			((val & 0x0020) << 3) | ((val & 0x2000) >> 4) |
			((val & 0x0010) << 8) | ((val & 0x1000) << 1);
		*(u16*)(vramptr + 2) = ((*(u16*)(vramptr + 2)) & 0xCCCC) | 
			((val & 0x0008) >> 3) | ((val & 0x0800) >> 10) | 
			((val & 0x0004) << 2) | ((val & 0x0400) >> 5) |
			((val & 0x0002) << 7) | ((val & 0x0200)) |
			((val & 0x0001) << 12) | ((val & 0x0100) << 5);
	}
}

inline void PPU_UpdateVRAM(u32 addr, u16 val)
{
	PPU_VRAMBlock* block = &PPU_VRAMMap[addr >> 11];
	
	if (block->ChrUsage & 0x0001) PPU_UpdateVRAM_CHR(0, addr, val);
	if (block->ChrUsage & 0x0002) PPU_UpdateVRAM_CHR(1, addr, val);
	if (block->ChrUsage & 0x0004) PPU_UpdateVRAM_CHR(2, addr, val);
	if (block->ChrUsage & 0x0008) PPU_UpdateVRAM_CHR(3, addr, val);
	if (block->ChrUsage & 0x0010) PPU_UpdateVRAM_OBJ(addr, val);
	
	if (block->ScrUsage & 0x0001) PPU_UpdateVRAM_SCR(0, addr, val);
	if (block->ScrUsage & 0x0002) PPU_UpdateVRAM_SCR(1, addr, val);
	if (block->ScrUsage & 0x0004) PPU_UpdateVRAM_SCR(2, addr, val);
	if (block->ScrUsage & 0x0008) PPU_UpdateVRAM_SCR(3, addr, val);
}

void PPU_UpdateOAM(u16 addr, u16 val)
{
	if (addr < 0x200)
	{
		//u16* oam = (u16*)(0x07000000 + ((addr & 0xFFFFFFFC) << 1));
		u16* oam = &PPU_OBJList[addr & 0xFFFFFFFC];
		switch (addr & 0x3)
		{
			case 0x0:	// X coord low, Y coord
				oam[0] = (oam[0] & 0xC000) | (((val >> 8) - PPU_YOffset + 1) & 0xFF);
				oam[1] = (oam[1] & 0xFF00) | (val & 0xFF);
				break;
				
			case 0x2:  	// tile num low, attrib
				{
					oam[1] = (oam[1] & 0xFFFFCFFF) | ((val & 0xC000) >> 2);
					
					// TODO prio
					oam[2] = (oam[2] & 0x0C00) | ((val & 0x01F0) << 1) | (val & 0x000F) | ((val & 0x0E00) << 3) | 0x8000;
					
					// bit0-8: tile num
					// bit9-11: pal
					// bit12-13: prio
					// bit14: Xflip
					// bit15: Yflip
				}
				break;
		}
	}
	else
	{
		addr &= 0x1F;
		//u16* oam = (u16*)(0x07000000 + (addr << 5));
		u16* oam = &PPU_OBJList[addr << 4];
		
		int i;
		for (i = 0; i < 4; i++)
		{
			u16 oval = oam[1] & 0xFFFF3EFF;
			if (val & 0x1) oval |= 0x0100;
			
			u8 objsize = PPU_OBJSizes[(val & 0x2) >> 1];
			oval |= (objsize & 0x3) << 14;
			oam[1] = oval;
			
			oval = oam[0] & 0xFFFF3FFF;
			if (objsize & 0x80) oval |= 0x8000;
			oam[0] = oval;
			
			val >>= 2;
			oam += 4;
		}
	}
}


// I/O
// addr = lowest 8 bits of address in $21xx range

u8 PPU_Read8(u32 addr)
{
	asm("stmdb sp!, {r2-r3, r12}");
	
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
			
		case 0x38:
			ret = PPU_OAM[PPU_OAMAddr];
			PPU_OAMAddr++;
			break;
		
		case 0x39:
			{
				addr = (PPU_VRAMAddr << 1) & 0xFFFEFFFF;
				ret = PPU_VRAM[addr];
				if (!(PPU_VRAMInc & 0x80))
					PPU_VRAMAddr += PPU_VRAMStep;
			}
			break;
		case 0x3A:
			{
				addr = (PPU_VRAMAddr << 1) & 0xFFFEFFFF;
				ret = PPU_VRAM[addr + 1];
				if (PPU_VRAMInc & 0x80)
					PPU_VRAMAddr += PPU_VRAMStep;
			}
			break;
		
		case 0x40: ret = IPC->SPC_IOPorts[4]; break;
		case 0x41: ret = IPC->SPC_IOPorts[5]; break;
		case 0x42: ret = IPC->SPC_IOPorts[6]; break;
		case 0x43: ret = IPC->SPC_IOPorts[7]; break;
	}
	
	asm("ldmia sp!, {r2-r3, r12}");
	return ret;
}

u16 PPU_Read16(u32 addr)
{
	asm("stmdb sp!, {r2-r3, r12}");
	
	u16 ret = 0;
	switch (addr)
	{
		// not in the right place, but well
		// our I/O functions are mapped to the whole $21xx range
		
		case 0x40: ret = *(u16*)&IPC->SPC_IOPorts[4]; break;
		case 0x42: ret = *(u16*)&IPC->SPC_IOPorts[6]; break;
		
		default:
			ret = PPU_Read8(addr);
			ret |= (PPU_Read8(addr+1) << 8);
			break;
	}
	
	asm("ldmia sp!, {r2-r3, r12}");
	return ret;
}

void PPU_Write8(u32 addr, u8 val)
{
	asm("stmdb sp!, {r2-r3, r12}");
	
	switch (addr)
	{
		case 0x00:
			break;
			
		case 0x01:
			{
				PPU_OBJSize = val >> 5;
				PPU_OBJSizes = _PPU_OBJSizes + (PPU_OBJSize << 1);
				u16 base = (val & 0x07) << 14;
				u16 gap = (val & 0x18) << 10;
				PPU_SetOBJCHR(base, gap);
				iprintf("OBJ base:%08X gap:%08X | %08X\n", base, gap, (u32)&PPU_VRAM + base);
			}
			break;
			
		case 0x02:
			PPU_OAMAddr = (PPU_OAMAddr & 0x200) | (val << 1);
			break;
		case 0x03:
			PPU_OAMAddr = (PPU_OAMAddr & 0x1FE) | ((val & 0x01) << 9);
			PPU_OAMPrio = val & 0x80;
			break;
			
		case 0x04:
			if (PPU_OAMAddr >= 0x200)
			{
				u16 addr = PPU_OAMAddr;
				addr &= 0x21F;
				
				if (PPU_OAM[addr] != val)
				{
					PPU_OAM[addr] = val;
					PPU_UpdateOAM(addr, val);
					PPU_OAMDirty = 1;
				}
			}
			else if (PPU_OAMAddr & 0x1)
			{
				PPU_OAMVal |= (val << 8);
				u16 addr = PPU_OAMAddr - 1;
				
				if (*(u16*)&PPU_OAM[addr] != PPU_OAMVal)
				{
					*(u16*)&PPU_OAM[addr] = PPU_OAMVal;
					PPU_UpdateOAM(addr, PPU_OAMVal);
					PPU_OAMDirty = 1;
				}
			}
			else
			{
				PPU_OAMVal = val;
			}
			PPU_OAMAddr++;
			PPU_OAMAddr &= 0x3FF;
			break;
			
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
			if ((val & 0x7C) != 0x00) iprintf("UNSUPPORTED VRAM MODE %02X\n", val);
			//printf("vram = %08X\n", (u32)&PPU_VRAM);
			PPU_VRAMInc = val;
			switch (val & 0x03)
			{
				case 0x00: PPU_VRAMStep = 1; break;
				case 0x01: PPU_VRAMStep = 32; break;
				case 0x02:
				case 0x03: PPU_VRAMStep = 128; break;
			}
			break;
			
		case 0x16:
			PPU_VRAMAddr &= 0xFF00;
			PPU_VRAMAddr |= val;
			PPU_VRAMPref = *(u16*)&PPU_VRAM[(PPU_VRAMAddr << 1) & 0xFFFEFFFF];
			break;
		case 0x17:
			PPU_VRAMAddr &= 0x00FF;
			PPU_VRAMAddr |= (u16)val << 8;
			PPU_VRAMPref = *(u16*)&PPU_VRAM[(PPU_VRAMAddr << 1) & 0xFFFEFFFF];
			break;
		
		case 0x18: // VRAM shit
			{
				addr = (PPU_VRAMAddr << 1) & 0xFFFEFFFF;
				if (PPU_VRAM[addr] != val)
				{
					PPU_VRAM[addr] = val;
					PPU_UpdateVRAM(addr, *(u16*)&PPU_VRAM[addr]);
				}
				if (!(PPU_VRAMInc & 0x80))
					PPU_VRAMAddr += PPU_VRAMStep;
			}
			break;
		case 0x19:
			{
				addr = ((PPU_VRAMAddr << 1) + 1) & 0xFFFEFFFF;
				if (PPU_VRAM[addr] != val)
				{
					PPU_VRAM[addr] = val;
					addr--;
					PPU_UpdateVRAM(addr, *(u16*)&PPU_VRAM[addr]);
				}
				if (PPU_VRAMInc & 0x80)
					PPU_VRAMAddr += PPU_VRAMStep;
			}
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
				PPU_CGRAMAddr++;
				PPU_CGRDirty = 1;
			}
			PPU_CGRFlag = !PPU_CGRFlag;
			break;
		
		case 0x2C:
			PPU_BGMain = val & 0x1F;
			*(u32*)0x04000000 &= 0xFFFFE0FF;
			*(u32*)0x04000000 |= ((PPU_BGMain | PPU_BGSub) << 8);
			break;
		case 0x2D:
			// TODO subscreen handling for funky hires modes
			PPU_BGSub = val & 0x1F;
			*(u32*)0x04000000 &= 0xFFFFE0FF;
			*(u32*)0x04000000 |= ((PPU_BGMain | PPU_BGSub) << 8);
			break;
			
		case 0x40: IPC->SPC_IOPorts[0] = val; break;
		case 0x41: IPC->SPC_IOPorts[1] = val; break;
		case 0x42: IPC->SPC_IOPorts[2] = val; break;
		case 0x43: IPC->SPC_IOPorts[3] = val; break;
				
		default:
			//iprintf("PPU_Write8(%08X, %08X)\n", addr, val);
			break;
	}
	
	asm("ldmia sp!, {r2-r3, r12}");
}

void PPU_Write16(u32 addr, u16 val)
{
	asm("stmdb sp!, {r2-r3, r12}");
	
	switch (addr)
	{
		// optimized route
		
		case 0x16:
			PPU_VRAMAddr = val;
			break;
			
		case 0x40: *(u16*)&IPC->SPC_IOPorts[0] = val; break;
		case 0x42: *(u16*)&IPC->SPC_IOPorts[2] = val; iprintf("2142: %04X\n", val); break;
		
		case 0x41:
		case 0x43: iprintf("!! write $21%02X %04X\n", addr, val); break;
		
		// otherwise, just do two 8bit writes
		default:
			PPU_Write8(addr, val & 0x00FF);
			PPU_Write8(addr+1, val >> 8);
			break;
	}
	
	asm("ldmia sp!, {r2-r3, r12}");
}


ITCM_CODE void PPU_VBlank()
{
	int i;
	
	if (PPU_CGRDirty)
	{
		PPU_CGRDirty = 0;
		
		*(u8*)0x04000245 = 0x80;
		*(u8*)0x04000246 = 0x80;
		
		PPU_UploadBGPal(0, false);
		PPU_UploadBGPal(1, false);
		PPU_UploadBGPal(2, false);
		PPU_UploadBGPal(3, false);
			
		*(u8*)0x04000245 = 0x84;
		*(u8*)0x04000246 = 0x8C;
	}
	
	u8 firstoam = (PPU_OAMAddr & 0xFE) >> 1;
	if (PPU_OAMDirty || firstoam != PPU_FirstOAM)
	{
		PPU_OAMDirty = 0;
		PPU_FirstOAM = firstoam;
		
		register u32 srcaddr = firstoam << 2;
		register u16* dst = (u16*)0x07000000;
		register int nsprites = 0;
		
		// insert faketile OBJs here
		
		for (i = 0; i < 128; i++)
		{
			u16 attr0 = PPU_OBJList[srcaddr++];
			u16 attr1 = PPU_OBJList[srcaddr++];
			
			u8 w = PPU_SpriteSize[(attr1 >> 14) | ((attr0 & 0x8000) >> 13)];
			u8 h = PPU_SpriteSize[(attr1 >> 14) | ((attr0 & 0x8000) >> 13) | 0x8];
			
			// if the sprite is offscreen, don't deal with it
			u16 y = attr0 & 0xFF;
			u16 x = attr1 & 0x1FF;
			if ((x > 0xFF && x <= (0x200 - w)) || (y > 0xBF && y <= (0x100 - h)))
			{
				srcaddr += 2;
				srcaddr &= 0x1FF;
				continue;
			}
			
			*dst++ = attr0;
			*dst++ = attr1;
			*dst++ = PPU_OBJList[srcaddr++];
			*dst++ = PPU_OBJList[srcaddr++];
			
			srcaddr &= 0x1FF;
			nsprites++;
		}
		
		// disable all the sprites we didn't use
		for (i = nsprites; i < 128; i++)
		{
			*dst++ = 0x0200;
			*dst++ = 0x0000;
			*dst++ = 0x0000;
			*dst++ = 0x0000;
		}
	}
}
