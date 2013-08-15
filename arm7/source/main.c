/*---------------------------------------------------------------------------------

	default ARM7 core

		Copyright (C) 2005 - 2010
		Michael Noland (joat)
		Jason Rogers (dovoto)
		Dave Murphy (WinterMute)

	This software is provided 'as-is', without any express or implied
	warranty.  In no event will the authors be held liable for any
	damages arising from the use of this software.

	Permission is granted to anyone to use this software for any
	purpose, including commercial applications, and to alter it and
	redistribute it freely, subject to the following restrictions:

	1.	The origin of this software must not be misrepresented; you
		must not claim that you wrote the original software. If you use
		this software in a product, an acknowledgment in the product
		documentation would be appreciated but is not required.

	2.	Altered source versions must be plainly marked as such, and
		must not be misrepresented as being the original software.

	3.	This notice may not be removed or altered from any source
		distribution.

---------------------------------------------------------------------------------*/
#include <nds.h>

#include "spc700.h"


int main() 
{
	readUserSettings();

	irqInit();
	fifoInit();

	installSystemFIFO();
	
	// wait till the ARM9 has mapped VRAM for us
	for (;;)
	{
		u8 vrammap = *(volatile u8*)0x04000240;
		if (vrammap & 0x01) break;
	}
	
	// then proceed to copy shit
	int i;
	u32* src = (u32*)&SPC_ROM[0];
	u32* dst = (u32*)0x0600FFC0;
	for (i = 0; i < 64; i += 4)
		*dst++ = *src++;
	
	*(u32*)0x04000210 |= 0x00000008;
	*(u16*)0x04000100 = 0xFBE9;
	*(u16*)0x04000102 = 0x00C0;

	for (;;)
	{
		u32 val = fifoGetValue32(FIFO_USER_01);
		if (val)
		{
			switch (val)
			{
				case 1:
					SPC_Reset();
					break;
				
				case 2:
					swiWaitForVBlank();
					SPC_Run();
					break;
					
				case 3:
					while (!fifoCheckAddress(FIFO_USER_01));
					SPC_IOPorts = fifoGetAddress(FIFO_USER_01);
					break;
			}
		}
		
		swiWaitForVBlank();
	}

	return 0;
}
