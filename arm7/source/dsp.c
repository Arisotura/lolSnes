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

#include "spc700.h"
#include "helper.h"


u8 DSP_Regs[0x80];

// buffer size should be power of two
#define BUFFER_SIZE 2048
//s16 DSP_Buffer[BUFFER_SIZE] ALIGN(4);
#define DSP_LBuffer ((s16*)0x06010040)
#define DSP_RBuffer ((s16*)(0x06010040 + (BUFFER_SIZE << 1)))
u32 DSP_CurSample = 0;

#define SAMPLES_PER_ITER 16
//s16 DSP_TempBuffer[SAMPLES_PER_ITER] ALIGN(4);

#define DSP_BRRTable ((s16*)0x0601FE00)

// BRR cache:
// start: 06012040
// end:   0601FA00
// size:  D9C0
// 1639*34 (1639 blocks)

// compressed Gauss table
// 0XXX: copy value XXX once
// 80XX: repeat following value X times
// 4XYY: repeat following value X times, then increment, do that Y times
// 2XYY: increment following value by X, do that Y times
// FFFF: stop
u16 comp_gauss[] = 
{
	0x8010, 0x0000, 0x800B, 0x0001, 0x8007, 0x0002, 0x8005, 0x0003, 
	0x8005, 0x0004, 0x8004, 0x0005, 0x8004, 0x0006, 0x8003, 0x0007, 
	0x8003, 0x0008, 0x8003, 0x0009, 0x8003, 0x000A, 0x8003, 0x000B, 
	0x4203, 0x000C, 0x8003, 0x000F, 0x4202, 0x0010, 0x0012, 0x4203, 
	0x0013, 0x0016, 0x4202, 0x0017, 0x0019, 0x001A, 0x001B, 0x001B,  
	0x001C, 0x001D, 0x001D, 0x001E, 0x001F, 0x0020, 0x2105, 0x0020, 
	0x2115, 0x0024, 0x2105, 0x003A, 0x2104, 0x0040, 0x2103, 0x0045, 
	0x0049, 0x004A, 0x2103, 0x004C, 0x0050, 0x0051, 0x0053, 0x0054, 
	0x0056, 0x0057, 0x0059, 0x005A, 0x005C, 0x005E, 0x2203, 0x005F, 
	0x2204, 0x0064, 0x2206, 0x006B, 0x2209, 0x0076, 0x2206, 0x0089, 
	0x2204, 0x0096, 0x2203, 0x009F, 0x00A6, 0x00A8, 0x2203, 0x00AB, 
	0x00B2, 0x00B4, 0x00B7, 0x00BA, 0x00BC, 0x00BF, 0x2303, 0x00C1, 
	0x2304, 0x00C9, 0x2312, 0x00D4, 0x2304, 0x010B, 0x2303, 0x0118, 
	0x0122, 0x0125, 0x0129, 0x012C, 0x0130, 0x0133, 0x0137, 0x013A, 
	0x013E, 0x0141, 0x0145, 0x0148, 0x014C, 0x0150, 0x0153, 0x0157, 
	0x015B, 0x015F, 0x2407, 0x0162, 0x2407, 0x017D, 0x2407, 0x019A, 
	0x2404, 0x01B7, 0x2403, 0x01C8, 0x2403, 0x01D5, 0x01E2, 0x01E6, 
	0x2403, 0x01EB, 0x01F8, 0x01FC, 0x0201, 0x2503, 0x0205, 0x0213, 
	0x0218, 0x2503, 0x021C, 0x022A, 0x022F, 0x2503, 0x0233, 0x2504, 
	0x0241, 0x2504, 0x0254, 0x2506, 0x0267, 0x2507, 0x0284, 0x250B, 
	0x02A6, 0x250F, 0x02DC, 0x250A, 0x0326, 0x2506, 0x0357, 0x2505, 
	0x0374, 0x2504, 0x038C, 0x2503, 0x039F, 0x2503, 0x03AD, 0x2503, 
	0x03BB, 0x03C9, 0x03CE, 0x2503, 0x03D2, 0x03E0, 0x2403, 0x03E5, 
	0x03F2, 0x03F6, 0x2403, 0x03FB, 0x2403, 0x0408, 0x2405, 0x0415, 
	0x042A, 0x240A, 0x042E, 0x2405, 0x0455, 0x2403, 0x0468, 0x0473, 
	0x0477, 0x047A, 0x047E, 0x0481, 0x0485, 0x0488, 0x048C, 0x048F, 
	0x0492, 0x0496, 0x2304, 0x0499, 0x2306, 0x04A6, 0x2305, 0x04B7, 
	0x2303, 0x04C5, 0x04CD, 0x04D0, 0x04D2, 0x2203, 0x04D5, 0x2203, 
	0x04DC, 0x220A, 0x04E3, 0x2203, 0x04F6, 0x2203, 0x04FB, 0x0500, 
	0x2103, 0x0502, 0x2103, 0x0506, 0x2108, 0x050A, 0x2104, 0x0511, 
	0x2103, 0x0514, 0x0516, 0x8003, 0x0517, 0x8005, 0x0518, 0x0519, 
	0x0519, 0xFFFF
};
#define DSP_GaussTable ((s16*)0x0601FA00)

extern int itercount;

typedef struct
{
	int Status;
	
	u16 Pitch;
	u16 FinalPitch;
	u16 ParamAddr;
	u16 CurAddr;
	u16 LoopBlock;
	u16 LoopAddr;
	u32 Position;
	
	// volume crapo
	s8 LeftVolume;
	s8 RightVolume;
	
	// BRR decoding
	u16 CurBlock;
	u8 CurBlockVal;
	
	u32 CurSample;
	
	s32 CurBlockSamples[3 + 16];
	
} DSP_Voice;
DSP_Voice DSP_Voices[8];


//#define BRRCACHE_SIZE 1925
//#define DSP_BRRCache (s16*)0x06010040
//#define DSP_BRRTable (u16*)0x0601F0E0


void DSP_Reset()
{
	int i;
	
	for (i = 0; i < 0x80; i++)
		DSP_Regs[i] = 0;
		
	for (i = 0; i < BUFFER_SIZE; i++)
	{
		DSP_LBuffer[i] = 0;
		DSP_RBuffer[i] = 0;
	}
	DSP_CurSample = 0;
		
	for (i = 0; i < 8; i++)
	{
		DSP_Voice* voice = &DSP_Voices[i];
		
		voice->Status = 0;
		voice->ParamAddr = 0;
		voice->CurAddr = 0;
		voice->LoopAddr = 0;
		voice->Position = 0;
		voice->Pitch = 0;
		
		int j;
		for (j = 0; j < 3 + 16; j++)
			voice->CurBlockSamples[j] = 0;
	}
	
	
	u16* gsrc = &comp_gauss[0];
	u16* gdst = &DSP_GaussTable[0];
	for (;;)
	{
		u16 val = *gsrc++;
		if (val == 0xFFFF) break;
		
		u16 a = (val & 0x0F00) >> 8;
		u16 b = val & 0xFF;
		
		switch (val & 0xF000)
		{
			case 0x0000:
				*gdst++ = val;
				break;
			
			case 0x8000:
				{
					u16 rep = *gsrc++;
					for (i = 0; i < b; i++)
						*gdst++ = rep;
				}
				break;
				
			case 0x4000:
				{
					u16 rep = *gsrc++;
					for (i = 0; i < b; i++)
					{
						u16 j;
						for (j = 0; j < a; j++)
							*gdst++ = rep;
						rep++;
					}
				}
				break;
				
			case 0x2000:
				{
					u16 rep = *gsrc++;
					for (i = 0; i < b; i++)
					{
						*gdst++ = rep;
						rep += a;
					}
				}
				break;
		}
	}
	
	for (i = 0; i < 256; i++)
	{
		int shift = i >> 4;
		s16 samp = i & 0xF;
		
		if (samp & 0x8) samp |= 0xFFF0;
		if (shift > 12) { samp = (samp & 0x8) ? 0xF000 : 0x0000; }
		else samp <<= shift;
		samp >>= 1;
		
		DSP_BRRTable[i] = samp;
	}
	
	
	for (i = 0; i < 0x100; i += 0x10)
		*(vu32*)(0x04000400 + i) = 0;
		
	// turn sound on, wait 15ms
	*(vu8*)0x04000304 |= 0x01;
	swiDelay(125678);
	
	*(vu16*)0x04000500 = 0x807F;
	*(vu16*)0x04000504 = 0x0200;
	
	//u16 timerval = 0xFDF5; // 32KHz
	u16 timerval = 0xFD46; // 24KHz
	
	*(vu32*)0x04000404 = (u32)&DSP_LBuffer[0];
	*(vu16*)0x04000408 = timerval;
	*(vu16*)0x0400040A = 0x0000;
	*(vu32*)0x0400040C = BUFFER_SIZE >> 1;
	
	*(vu32*)0x04000414 = (u32)&DSP_RBuffer[0];
	*(vu16*)0x04000418 = timerval;
	*(vu16*)0x0400041A = 0x0000;
	*(vu32*)0x0400041C = BUFFER_SIZE >> 1;
	
	*(vu32*)0x04000400 = 0xA800007F;
	*(vu32*)0x04000410 = 0xA87F007F;
}


inline s32 DSP_Clamp(s32 val)
{
	s32 signbits = val & 0x00018000;
	if (signbits == 0x00010000) // below -0x8000
		return -0x8000;
	else if (signbits == 0x00008000) // above 0x7FFF
		return 0x7FFF;
		
	return val;
}

// BRR filters: buf points to the current sample

void DSP_BRRFilter1(s32 samp, s32* buf)
{
	buf[0] += buf[-1] + ((-buf[-1]) >> 4);
}

void DSP_BRRFilter2(s32 samp, s32* buf)
{
	buf[0] += (buf[-1] << 1) + ((-buf[-1] * 3) >> 5) - buf[-2] + (buf[-2] >> 4);
}

void DSP_BRRFilter3(s32 samp, s32* buf)
{
	buf[0] += (buf[-1] << 1) + ((-buf[-1] * 13) >> 6) - buf[-2] + ((buf[-2] * 3) >> 4);
}

void (*DSP_BRRFilters[4])(s32, s32*) = { 0, DSP_BRRFilter1, DSP_BRRFilter2, DSP_BRRFilter3 };

u32 brrtime = 0;
void DSP_DecodeBRR(int nv, DSP_Voice* voice)
{
	//*(vu32*)0x0400010C = 0x00800000;
	
	register u8* data = &SPC_RAM[voice->CurAddr];
	register s32* dst = &voice->CurBlockSamples[3];
	
	dst[-3] = dst[13];
	dst[-2] = dst[14];
	dst[-1] = dst[15];
	
	register u8 blockval = *data++;
	register u8 shift = blockval & 0xF0;
	register void (*brrfilter)(s32, s32*) = DSP_BRRFilters[(blockval & 0x0C) >> 2];
	
	int i;
	for (i = 8; i > 0; i--)
	{
		u8 byte = *data++;
		
		s32 samp = (s32)DSP_BRRTable[(byte >> 4) | shift];
		*dst = samp;
		if (brrfilter) (*brrfilter)(samp, dst);
		dst++;
		
		samp = (s32)DSP_BRRTable[(byte & 0x0F) | shift];
		*dst = samp;
		if (brrfilter) (*brrfilter)(samp, dst);
		dst++;
	}
	
	voice->CurAddr += 9;
	
	if (blockval & 0x01) DSP_Regs[0x7C] |= (1 << nv);
	
	//brrtime += *(vu16*)0x0400010C;
	//*(vu32*)0x0400010C = 0x00000000;
}

void DSP_Mix()
{
	int i, j;
	
	//brrtime = 0;
	//*(vu32*)0x04000108 = 0x00800000;
	
	register s16* lbuf = &DSP_LBuffer[DSP_CurSample];
	register s16* rbuf = &DSP_RBuffer[DSP_CurSample];
	zerofillBuffers(lbuf, rbuf);
	
	register DSP_Voice* voice = &DSP_Voices[0];
	for (i = 0; i < 8; i++)
	{
		if (!voice->Status) 
		{
			voice++;
			continue;
		}
		
		register u32 pos = voice->Position;
		register u8 blockval = voice->CurBlockVal;
		register s32* sbuf = &voice->CurBlockSamples[0];
		
		for (j = 0; j < SAMPLES_PER_ITER; j++)
		{
			int _pos = 3 + ((pos >> 12) & 0xF);
			s32 samp = sbuf[_pos];
			
			// interpolation
			/*int interp = (pos >> 4) & 0xFF;
			samp = 	((samp * DSP_GaussTable[interp]) >> 11) + 
					((voice->CurBlockSamples[_pos-1] * DSP_GaussTable[0x100+interp]) >> 11) +
					((voice->CurBlockSamples[_pos-2] * DSP_GaussTable[0x1FF-interp]) >> 11) +
					((voice->CurBlockSamples[_pos-3] * DSP_GaussTable[0x0FF-interp]) >> 11);*/
			//if (voice->Pitch < 0x1000)
			{
				int interp = (pos >> 4) & 0xFF;
				samp = ((samp * interp) + (sbuf[_pos-1] * (0xFF-interp))) >> 8;
			}
			
			// TODO: apply ADSR here
			
			*lbuf = DSP_Clamp(*lbuf + ((samp * voice->LeftVolume) >> 7));
			*rbuf = DSP_Clamp(*rbuf + ((samp * voice->RightVolume) >> 7));
			lbuf++; rbuf++;
			
			pos += voice->FinalPitch;
			
			u16 block = pos >> 16;
			if (block != voice->CurBlock)
			{
				if (blockval & 0x01)
				{
					if (blockval & 0x02)
					{
						voice->CurAddr = voice->LoopAddr;
						voice->CurBlockVal = SPC_RAM[voice->CurAddr];
						
						voice->CurBlock = voice->LoopBlock;
						voice->Position = (voice->CurBlock << 16) | (pos & 0x0000FFFF);
						voice->CurSample = -1;
						
						pos = voice->Position;
						blockval = voice->CurBlockVal;
					}
					else
					{
						// TODO proper ADSR release
						voice->Status = 0;
						break;
					}
				}
				else
				{
					voice->CurBlock = block;
					voice->CurBlockVal = SPC_RAM[voice->CurAddr];
					blockval = voice->CurBlockVal;
				}
				
				DSP_DecodeBRR(i, voice);
			}
		}
		
		voice->Position = pos;
		
		voice++;
		lbuf -= SAMPLES_PER_ITER;
		rbuf -= SAMPLES_PER_ITER;
	}
	
	/*for (i = 0; i < SAMPLES_PER_ITER; i++)
	{
		DSP_Buffer[DSP_CurSample++] = DSP_TempBuffer[i];//(s16)sample;//(sample ^ 0xFFFF);
		DSP_CurSample &= (BUFFER_SIZE - 1);
	}*/
	DSP_CurSample += SAMPLES_PER_ITER;
	DSP_CurSample &= (BUFFER_SIZE - 1);
	
	//u16 mixtime = *(vu16*)0x04000108;
	//*(vu32*)0x04000108 = 0x00000000;
	//if (mixtime > 1000) spcPrintf("mixer: %d | BRR: %d\n", mixtime, brrtime);
}


u8 DSP_Read(u8 reg)
{
	reg &= 0x7F;
	
	return DSP_Regs[reg];
}

void DSP_Write(u8 reg, u8 val)
{
	int i;
	
	if (reg & 0x80) return;
	
	DSP_Regs[reg] = val;
	
	if (reg == 0x4C) // key on
	{
		for (i = 0; i < 8; i++)
		{
			if (!(val & (1 << i))) continue;
			
			DSP_Voice* voice = &DSP_Voices[i];
			voice->ParamAddr = (DSP_Regs[0x5D] << 8) + (DSP_Regs[(i<<4)+0x4] << 2);
			voice->Position = 0;
			voice->CurBlock = 0;
			
			voice->CurAddr = SPC_RAM[voice->ParamAddr] | (SPC_RAM[voice->ParamAddr + 1] << 8);
			voice->LoopAddr = SPC_RAM[voice->ParamAddr + 2] | (SPC_RAM[voice->ParamAddr + 3] << 8);
			voice->LoopBlock = (voice->LoopAddr - voice->CurAddr) / 9;
			
			voice->CurBlockVal = SPC_RAM[voice->CurAddr];
			//voice->CurSample = -1;
			
			DSP_DecodeBRR(i, voice);
			voice->Status = 1;
		}
	}
	else if (reg == 0x5C) // key off
	{
		for (i = 0; i < 8; i++)
		{
			if (!(val & (1 << i))) continue;
			
			DSP_Voice* voice = &DSP_Voices[i];
			voice->Status = 0;
			// TODO proper ADSR release
		}
	}
	else if (reg == 0x7C)
	{
		DSP_Regs[0x7C] = 0;
	}
	else
	{
		DSP_Voice* voice = &DSP_Voices[reg >> 4];
		
		switch (reg & 0x0F)
		{
			case 0x00:
				voice->LeftVolume = (s8)val;
				break;
			case 0x01:
				voice->RightVolume = (s8)val;
				break;
				
			case 0x02:
				voice->Pitch &= 0x3F00;
				voice->Pitch |= val;
				voice->FinalPitch = voice->Pitch + (voice->Pitch / 3);
				break;
			case 0x03:
				voice->Pitch &= 0x00FF;
				voice->Pitch |= ((val & 0x3F) << 8);
				voice->FinalPitch = voice->Pitch + (voice->Pitch / 3);
				break;
		}
	}
}
