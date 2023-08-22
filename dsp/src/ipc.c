#include "teak/teak.h"
#include "ipc.h"
#include "ppu.h"


void ipc_init(void)
{
    REG_APBP_CONTROL = APBP_CONTROL_IRQ_CMD1_DISABLE | APBP_CONTROL_IRQ_CMD2_DISABLE;
    apbp_clearSemaphore(0xFFFF);
    cpu_disableIrqs();
    REG_ICU_IRQ_MODE |= ICU_IRQ_MASK_APBP;
    REG_ICU_IRQ_POLARITY &= ~ICU_IRQ_MASK_APBP;
    REG_ICU_IRQ_DISABLE &= ~ICU_IRQ_MASK_APBP;
    REG_ICU_IRQ_ACK = ICU_IRQ_MASK_APBP;
    REG_ICU_IRQ_INT0 = ICU_IRQ_MASK_APBP;
    cpu_enableInt0();
    cpu_enableIrqs();
    apbp_sendData(0, 1);
    apbp_sendData(1, 1);
    apbp_sendData(2, 1);
}

void onIpcCommandReceived(void)
{
	u16 data = apbp_receiveData(0);
	switch (data & 0xC000)
	{
	case 0x0000: // IO write
		PPU_Write8(data >> 8, data & 0xFF);
		break;
		
	case 0x4000: // HBlank
		PPU_DrawScanline(data & 0x1FF);
		break;
		
	case 0x8000: // VBlank
		PPU_VBlank();
		break;
		
	case 0xC000: // reset
		PPU_Reset();
		apbp_sendData(0, 1);
		break;
	}
}