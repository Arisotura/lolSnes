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


void (*oldIRQHandler)();

void myIRQHandler()
{
	// handle IPC sync IRQ separately
	u32 IE = *(u32*)0x04000210;
	u32 IF = *(u32*)0x04000214;
	u32 irq = IE & IF;
	
	if (irq & 0x00010000)
	{
		*(u32*)0x04000214 = 0x00010000;
		*(u32*)0x0380FFF8 |= 0x00010000;
		irq &= 0xFFFEFFFF;
	}
	
	// pass down other IRQs to the libnds handler
	if (irq)
		oldIRQHandler();
}

int main() 
{
	readUserSettings();

	irqInit();
	fifoInit();

	installSystemFIFO();
	*(u16*)0x04000180 = 0x4000;
	
	//oldIRQHandler = (void(*)())(*(u32*)0x0380FFFC);
	//*(u32*)0x0380FFFC = (u32)(void*)myIRQHandler;

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
