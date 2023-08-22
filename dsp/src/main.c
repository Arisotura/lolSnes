#include "teak/teak.h"
#include "ipc.h"


int main()
{
	//*(vu16*)0x811A |= 0x0008;
	//*(vu16*)0x811A |= 0x0020;
	//*(vu16*)0x811A |= 0x0040;
	//cpu_disableIrqs();
	//*(vu16*)0x8114 = 0x0E30;
	//*(vu16*)0x811E = 0xC000; // relocate IO block to 0xF000
	//*(vu16*)0xC114 = 0x0E30;
	//*(vu16*)0xC114 = 0x1E20;
	//for (int a = 0; a < 5647627; a++) *(vu16*)0x0600 = a;
	//*(vu16*)0xC11E = 0x8000;
	//*(vu16*)0x8114 = 0x1E20;
	/*
	Teak Data Memory (RAM and Memory Mapped I/O)
Teak Data Memory is addressed via 16bit address bus (via registers r0..r7), allowing to access max 64Kwords (2Kwords of MMIO, plus 62Kwords of RAM). The memory is divided into three sections (X/Z/Y-spaces), the size/location of that sections can be changed via Port 8114h (in 1Kword units), and alongsides, the MMIO base can be adjusted via Port 811Eh. The default areas are:

  0000h..7FFFh  X Space (for RAM, with 1-stage write-buffer)     ;min zero
  8000h..87FFh  Z Space (for Memory-mapped I/O, no write-buffer) ;min zero
  8800h..FFFFh  Y Space (for RAM, with 1-stage write-buffer))    ;min 1Kword
*/
    icu_init();
    dma_init();

    //*(vu16*)0x0600 = 0;

    while(*(vu16*)0x80E0);

    ipc_init();
    
	
	u16 barf = 0;
	//*(vu16*)0x0600 = 0x801F;
    while(1)
    {
        //*(vu16*)0x0600 = color;
		//dma_transferDspToArm9((const void*)0x0600, 0x05000400, 1);
		/*for (int i = 0; i < 256; i++) praa[i] = *(vu16*)0x0600;
		dma_transferDspToArm9(praa, 0x06000000+(barf<<9), 256);
		barf++;
		barf &= 0xFF;*/
    }

    // u16 val = 0;
    // while(1)
    // {
    //     dma_transferArm9ToDsp(0x04000130, (void*)0x0601, 1);
    //     if(((*(vu16*)0x0601) & 1) == 0)
    //         setColor(val++);
    //     for(int i = 166666; i >= 0; i--)
    //         asm("nop");
    // }

    // while(1);

    // // while(((*(vu16*)0x818C) & 0x80) == 0);
    // // u16 color = 0;
    // // while(1)
    // // {
    // //     *(vu16*)0x0600 = color;
    // //     dma_transferDspToArm9((const void*)0x0600, 0x05000000, 1);
    // //     // for(int i = 1666666; i >= 0; i--)
    // //     // {
    // //     //     asm("nop");
    // //     // }
    // //     color++;
    // // }
    // //*(vu16*)0x0600 = 0x1F;
    // setColor(0x1F);
    // setColor(0x3F);
    // while(1);
    // //setColor(0x3E0);
    // *(vu16*)0x0600 = 123;
    // while(1);
    return 0;
}