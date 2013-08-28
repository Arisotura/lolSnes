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
// TODO buffers for left and right channels
#define BUFFER_SIZE 2048
s16 DSP_Buffer[BUFFER_SIZE] ALIGN(4);
u32 DSP_CurSample = 0;

#define SAMPLES_PER_ITER 16
s16 DSP_TempBuffer[SAMPLES_PER_ITER] ALIGN(4);

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
	
} DSP_Voice;
DSP_Voice DSP_Voices[8];


void DSP_Reset()
{
	int i;
	
	for (i = 0; i < 0x80; i++)
		DSP_Regs[i] = 0;
		
	for (i = 0; i < BUFFER_SIZE; i++)
		DSP_Buffer[i] = 0;
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
	
	
	for (i = 0; i < 0x100; i += 0x10)
		*(vu32*)(0x04000400 + i) = 0;
		
	// turn sound on, wait 15ms
	*(vu8*)0x04000304 |= 0x01;
	swiDelay(125678);
	
	*(vu16*)0x04000500 = 0x807F;
	*(vu16*)0x04000504 = 0x0200;
	
	*(vu32*)0x04000404 = (u32)&DSP_Buffer[0];
	*(vu16*)0x04000408 = 0xFDF5;
	*(vu16*)0x0400040A = 0x0000;
	*(vu32*)0x0400040C = BUFFER_SIZE >> 1;
	*(vu32*)0x04000400 = 0xA840007F;
}


void DSP_Mix()
{
	int i, j;
	
	for (i = 0; i < SAMPLES_PER_ITER; i += 2)
		*(u32*)&DSP_TempBuffer[i] = 0;
	
	for (i = 0; i < 8; i++)
	{
		register DSP_Voice* voice = &DSP_Voices[i];
		if (!voice->Status) continue;
		
		register u32 pos = voice->Position;
		register u8 blockval = voice->CurBlockVal;
		
		for (j = 0; j < SAMPLES_PER_ITER; j++)
		{
			if ((pos >> 12) != voice->CurSample)
			{
				voice->CurSample = pos >> 12;
				
				u8 byte = SPC_RAM[voice->CurAddr + ((pos & 0xE000) >> 13)];
				s32 samp = (pos & 0x1000) ? (byte & 0xF) : (byte >> 4);
				if (samp & 0x8) samp |= 0xFFFFFFF0;
				if (blockval >= 0xD0) { samp <<= 9; samp &= 0xFFFFF000; }
				else samp <<= (blockval >> 4);
				//samp >>= 1;
				
				switch (blockval & 0x0C)
				{
					case 0x00: break;
					case 0x04: samp += (voice->OldSamp + ((-voice->OldSamp) >> 4)); break;
					case 0x08: samp += ((voice->OldSamp << 1) + ((-voice->OldSamp * 3) >> 5)) - (voice->OlderSamp + (voice->OlderSamp >> 4)); break;
					case 0x0C: samp += ((voice->OldSamp << 1) + ((-voice->OldSamp * 13) >> 6)) - (voice->OlderSamp + ((voice->OlderSamp * 3) >> 4)); break;
				}
				
				// TODO many things
				
				voice->CurSamp = samp >> 1;//((samp * 127) >> 6);
				
				voice->OlderSamp = voice->OldSamp;
				voice->OldSamp = voice->CurSamp;
				
				//voice->CurSample++;
			}
			
			pos += voice->Pitch;
			DSP_TempBuffer[j] += voice->CurSamp;
			
			u16 block = pos >> 16;
			if (block != voice->CurBlock)
			{
				if (blockval & 0x01)
				{
					if (blockval & 0x02)
					{
						voice->CurAddr = voice->LoopAddr;
						voice->CurBlockVal = SPC_RAM[voice->CurAddr++];
						
						voice->CurBlock = voice->LoopBlock;
						voice->Position = voice->CurBlock << 16;
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
					voice->CurAddr += 8;
					voice->CurBlock = block;
					voice->CurBlockVal = SPC_RAM[voice->CurAddr++];
				}
			}
		}
		
		voice->Position = pos;
	}
	
	for (i = 0; i < SAMPLES_PER_ITER; i++)
	{
		DSP_Buffer[DSP_CurSample++] = DSP_TempBuffer[i];//(s16)sample;//(sample ^ 0xFFFF);
		DSP_CurSample &= (BUFFER_SIZE - 1);
	}
}

int lolmixed = 0;
void lolmix()
{
	if (lolmixed) return;
	lolmixed = 1;
	
	s16* dst = (s16*)0x06010040;
	//u8* src = (u8*)&SPC_RAM[0x8283];
	u8* src = (u8*)&SPC_RAM[0x8283];
	
	s32 old, older;
	
	int i, j;
	for (i = 0; i < 0x10000-0x40;)
	{
		u8 blockval = *src++;
		//if (blockval&0x1) break;
		u8 shift = blockval>>4;
		
		for (j = 0; j < 8; j++)
		{
			u8 byte = *src++;
			
			/*s32 samp = (byte >> 4) << 28;
			if (shift < 13) samp >>= (29 - shift);
			else { samp >>= 20; samp &= 0xFFFFF000; }*/
			s32 samp = byte >> 4;
			if (samp & 0x8) samp |= 0xFFFFFFF0;
			samp <<= shift;
			
			switch (blockval & 0x0C)
			{
				case 0x00: break;
				case 0x04: samp += (old + ((-old) >> 4)); break;
				case 0x08: samp += ((old << 1) + ((-old * 3) >> 5)) - (older + (older >> 4)); break;
				case 0x0C: samp += ((old << 1) + ((-old * 13) >> 6)) - (older + ((older * 3) >> 4)); break;
			}
			
			*dst++ = (s16)((samp * 127) >> 6);
			older = old;
			old = samp;
			i+=2;
			
			/*samp = byte << 28;
			if (shift < 13) samp >>= (29 - shift);
			else { samp >>= 20; samp &= 0xFFFFF000; }*/
			samp = byte & 0xF;
			if (samp & 0x8) samp |= 0xFFFFFFF0;
			samp <<= shift;
			
			switch (blockval & 0x0C)
			{
				case 0x00: break;
				case 0x04: samp += (old + ((-old) >> 4)); break;
				case 0x08: samp += ((old << 1) + ((-old * 3) >> 5)) - (older + (older >> 4)); break;
				case 0x0C: samp += ((old << 1) + ((-old * 13) >> 6)) - (older + ((older * 3) >> 4)); break;
			}
			
			*dst++ = (s16)((samp * 127) >> 6);
			older = old;
			old = samp;
			i+=2;
		}
		
		if (blockval & 0x01) break;
	}
	
	*(vu32*)0x04000404 = 0x06010040;
	*(vu16*)0x04000408 = 0xFF50;//0xFEA0;//(0xFDF5 * 0x17B9) / 0x1000;
	*(vu16*)0x0400040A = 0x0000;
	//*(vu32*)0x0400040C = 0x460>>1;//i >> 1;
	*(vu32*)0x04000400 = 0xA840007F;
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
			voice->Status = 1;
			voice->ParamAddr = (DSP_Regs[0x5D] << 8) + (DSP_Regs[(i<<4)+0x4] << 2);
			voice->Position = 0;
			voice->CurBlock = 0;
			
			voice->CurAddr = SPC_RAM[voice->ParamAddr] | (SPC_RAM[voice->ParamAddr + 1] << 8);
			voice->LoopAddr = SPC_RAM[voice->ParamAddr + 2] | (SPC_RAM[voice->ParamAddr + 3] << 8);
			voice->LoopBlock = (voice->LoopAddr - voice->CurAddr) / 9;
			
			voice->CurBlockVal = SPC_RAM[voice->CurAddr++];
			voice->CurSample = -1;
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
