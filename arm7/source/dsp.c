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

extern int itercount;

typedef struct
{
	int Status;
	
	u16 Pitch;
	u16 ParamAddr;
	u16 CurAddr;
	u16 LoopBlock;
	u16 LoopAddr;
	u32 Position;
	
	// BRR decoding
	u16 CurBlock;
	u8 CurBlockVal;
	
	u32 CurSample;
	s32 CurSamp, OlderSamp, OldSamp;
	
	s32 CurBlockSamples[16];
	
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
	
	*(vu32*)0x04000404 = (u32)&DSP_LBuffer[0];
	*(vu16*)0x04000408 = 0xFDF5;
	*(vu16*)0x0400040A = 0x0000;
	*(vu32*)0x0400040C = BUFFER_SIZE >> 1;
	
	*(vu32*)0x04000414 = (u32)&DSP_RBuffer[0];
	*(vu16*)0x04000418 = 0xFDF5;
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

/*s32 DSP_BRRFilter1(s32 samp, DSP_Voice* voice)
{
	return samp + voice->OldSamp + ((-voice->OldSamp) >> 4);
}

s32 DSP_BRRFilter2(s32 samp, DSP_Voice* voice)
{
	return samp + (voice->OldSamp << 1) + ((-voice->OldSamp * 3) >> 5) - voice->OlderSamp + (voice->OlderSamp >> 4);
}

s32 DSP_BRRFilter3(s32 samp, DSP_Voice* voice)
{
	return samp + (voice->OldSamp << 1) + ((-voice->OldSamp * 13) >> 6) - voice->OlderSamp + ((voice->OlderSamp * 3) >> 4);
}

s32 (*DSP_BRRFilters[4])(u32, DSP_Voice*) = { 0, DSP_BRRFilter1, DSP_BRRFilter2, DSP_BRRFilter3 };*/

void DSP_DecodeBRR(int nv, DSP_Voice* voice)
{
	register u8* data = &SPC_RAM[voice->CurAddr];
	register s32* dst = &voice->CurBlockSamples[0];
	
	u8 blockval = *data++;
	u8 shift = blockval & 0xF0;
	//s32 (*brrfilter)(s32, DSP_Voice*) = DSP_BRRFilters[(blockval & 0x0C) >> 2];
	
	int i;
	for (i = 8; i > 0; i--)
	{
		u8 byte = *data++;
		
		s32 samp = (s32)DSP_BRRTable[(byte >> 4) | shift];
		
		switch (blockval & 0x0C)
		{
			case 0x00: break;
			case 0x04: samp += voice->OldSamp + ((-voice->OldSamp) >> 4); break;
			case 0x08: samp += (voice->OldSamp << 1) + ((-voice->OldSamp * 3) >> 5) - voice->OlderSamp + (voice->OlderSamp >> 4); break;
			case 0x0C: samp += (voice->OldSamp << 1) + ((-voice->OldSamp * 13) >> 6) - voice->OlderSamp + ((voice->OlderSamp * 3) >> 4); break;
		}
		//if (brrfilter) samp = brrfilter(samp, voice);
		//samp = DSP_Clamp(samp >> 1) << 1;
		
		*dst++ = samp;
		voice->OlderSamp = voice->OldSamp;
		voice->OldSamp = samp;
		
		samp = (s32)DSP_BRRTable[(byte & 0x0F) | shift];
		
		switch (blockval & 0x0C)
		{
			case 0x00: break;
			case 0x04: samp += voice->OldSamp + ((-voice->OldSamp) >> 4); break;
			case 0x08: samp += (voice->OldSamp << 1) + ((-voice->OldSamp * 3) >> 5) - voice->OlderSamp + (voice->OlderSamp >> 4); break;
			case 0x0C: samp += (voice->OldSamp << 1) + ((-voice->OldSamp * 13) >> 6) - voice->OlderSamp + ((voice->OlderSamp * 3) >> 4); break;
		}
		//if (brrfilter) samp = brrfilter(samp, voice);
		//samp = DSP_Clamp(samp >> 1) << 1;
		
		*dst++ = samp;
		voice->OlderSamp = voice->OldSamp;
		voice->OldSamp = samp;
	}
	
	voice->CurAddr += 9;
	
	if (blockval & 0x01) DSP_Regs[0x7C] |= (1 << nv);
}
u32 maxtime = 0;
void DSP_Mix()
{
	int i, j;
	
	// benchmark code
	//*(vu32*)0x0400010C = 0x00840000;
	//*(vu32*)0x04000108 = 0x00800000;
	// benchmark end
	
	register int spos = DSP_CurSample;
	for (i = 0; i < SAMPLES_PER_ITER; i += 2)
	{
		*(u32*)&DSP_LBuffer[spos] = 0;
		*(u32*)&DSP_RBuffer[spos] = 0;
		spos += 2;
	}
	
	for (i = 0; i < 8; i++)
	{
		register DSP_Voice* voice = &DSP_Voices[i];
		if (!voice->Status) continue;
		
		register u32 pos = voice->Position;
		register u8 blockval = voice->CurBlockVal;
		
		spos = DSP_CurSample;
		for (j = 0; j < SAMPLES_PER_ITER; j++)
		{
			s32 samp = voice->CurBlockSamples[(pos >> 12) & 0xF];
			DSP_LBuffer[spos] = DSP_Clamp(DSP_LBuffer[spos] + samp);
			DSP_RBuffer[spos] = DSP_Clamp(DSP_RBuffer[spos] + samp);
			spos++;
			
			pos += voice->Pitch;
			
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
	}
	
	/*for (i = 0; i < SAMPLES_PER_ITER; i++)
	{
		DSP_Buffer[DSP_CurSample++] = DSP_TempBuffer[i];//(s16)sample;//(sample ^ 0xFFFF);
		DSP_CurSample &= (BUFFER_SIZE - 1);
	}*/
	DSP_CurSample += SAMPLES_PER_ITER;
	DSP_CurSample &= (BUFFER_SIZE - 1);
	
	// benchmark code
	//u16 timer_low = *(vu16*)0x04000108;
	//u16 timer_high = *(vu16*)0x0400010C;
	//u32 timer = timer_low | (timer_high << 16);
	//if (timer > maxtime) { maxtime = timer; spcPrintf("mixer time: %d cycles\n", timer); }
	// benchmark end
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
			case 0x02:
				voice->Pitch &= 0x3F00;
				voice->Pitch |= val;
				break;
			case 0x03:
				voice->Pitch &= 0x00FF;
				voice->Pitch |= ((val & 0x3F) << 8);
				break;
		}
	}
}
