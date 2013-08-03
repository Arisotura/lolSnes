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
			}
		}
		
		swiWaitForVBlank();
	}

	return 0;
}
