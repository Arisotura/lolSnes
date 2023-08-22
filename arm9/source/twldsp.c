#include <nds.h>
#include <stdio.h>

#include "twldsp.h"
#include "twlwram.h"
#include "scfg.h"


extern const dsp_dsp1_t lolSnes_cdc;
extern const int lolSnes_cdc_size;

u16 _slotB;
u16 _slotC;
int _codeSegs;
int _dataSegs;
u8 _codeSlots[TWR_WRAM_BC_SLOT_COUNT];
u8 _dataSlots[TWR_WRAM_BC_SLOT_COUNT];
u32 _flags;

void dsp_spinWait();
void twr_setPerm();


void dsp_setBlockReset(bool reset)
{
    REG_SCFG_RST = reset ? SCFG_RST_APPLY : SCFG_RST_RELEASE;
}

void dsp_setClockEnabled(bool enabled)
{
    if(enabled)
        REG_SCFG_CLK |= SCFG_CLK_DSP_ENABLE;
    else
        REG_SCFG_CLK &= ~SCFG_CLK_DSP_ENABLE;
}

void dsp_resetInterface()
{
    dsp_spinWait();
    if(!(REG_DSP_PCFG & DSP_PCFG_RESET))
        return;
    REG_DSP_PCFG &= ~(DSP_PCFG_IE_REP0 | DSP_PCFG_IE_REP1 | DSP_PCFG_IE_REP2);
    REG_DSP_PSEM = 0;
    REG_DSP_PCLEAR = 0xFFFF;
    //clear them all
    vu16 tmp = REG_DSP_REP0;
    tmp = REG_DSP_REP1;
    tmp = REG_DSP_REP2;
}

void dsp_setCoreResetOn()
{
    dsp_spinWait();
    if(REG_DSP_PCFG & DSP_PCFG_RESET)
        return;
    REG_DSP_PCFG |= DSP_PCFG_RESET;
    dsp_spinWait();
    while(REG_DSP_PSTS & DSP_PSTS_PERI_RESET);   
}

void dsp_setCoreResetOff(u16 repIrqMask)
{
    dsp_spinWait();
    while(REG_DSP_PSTS & DSP_PSTS_PERI_RESET);
    dsp_resetInterface();
    dsp_spinWait();
    REG_DSP_PCFG |= (repIrqMask & 7) << DSP_PCFG_IE_REP_SHIFT;
    dsp_spinWait();
    REG_DSP_PCFG &= ~DSP_PCFG_RESET;
}

void dsp_powerOn()
{
    dsp_setBlockReset(true);
    dsp_setClockEnabled(true);
    dsp_spinWait();
    dsp_setBlockReset(false);
    dsp_setCoreResetOn();
}

void dsp_powerOff()
{
    dsp_setBlockReset(true);
    dsp_setClockEnabled(false);
}

void dsp_sendData(int id, u16 data)
{
    dsp_spinWait();
    while(REG_DSP_PSTS & (1 << (DSP_PSTS_CMD_UNREAD_SHIFT + id)));
    (&REG_DSP_CMD0)[4 * id] = data;
}

u16 dsp_receiveData(int id)
{
    dsp_spinWait();
    while(!(REG_DSP_PSTS & (1 << (DSP_PSTS_REP_NEW_SHIFT + id))));
    return (&REG_DSP_REP0)[4 * id];
}

bool dsp_receiveDataReady(int id)
{
    dsp_spinWait();
    return (REG_DSP_PSTS & (1 << (DSP_PSTS_REP_NEW_SHIFT + id))) ? true : false;
}

void* DspToArm9Address(bool isCodePtr, u32 addr)
{
	addr = DSP_MEM_ADDR_TO_CPU(addr);
	int seg = addr >> TWR_WRAM_BC_SLOT_SHIFT;
	int offs = addr - (seg << TWR_WRAM_BC_SLOT_SHIFT);
	int slot = isCodePtr ? _codeSlots[seg] : _dataSlots[seg];
	return (u8*)twr_getBlockAddress(isCodePtr ? TWR_WRAM_BLOCK_B : TWR_WRAM_BLOCK_C) + slot * TWR_WRAM_BC_SLOT_SIZE + offs;
}

void TDSP_Init()
{
	twr_setPerm();
	
	REG_SCFG_EXT |= SCFG_EXT_ENABLE_DSP | SCFG_EXT_EXT_IRQ;
	twr_setBlockMapping(TWR_WRAM_BLOCK_A, TWR_WRAM_BASE, 0, TWR_WRAM_BLOCK_IMAGE_SIZE_32K);
	
	//map nwram
	twr_setBlockMapping(TWR_WRAM_BLOCK_B, 0x03800000, 256 * 1024, TWR_WRAM_BLOCK_IMAGE_SIZE_256K);
	twr_setBlockMapping(TWR_WRAM_BLOCK_C, 0x03C00000, 256 * 1024, TWR_WRAM_BLOCK_IMAGE_SIZE_256K);
	
	// load DSP binary
	TDSP_LoadBinary(&lolSnes_cdc);
	
	//enable dsp irqs
	*(vu32*)0x04000210 |= 1 << 24;
	
	u16 a = dsp_receiveData(0);
	u16 b = dsp_receiveData(1);
	u16 c = dsp_receiveData(2);
	iprintf("bourf %d/%d/%d\n", a, b, c);
}

void TDSP_SetMemoryMapping(bool isCode, u32 addr, u32 len, bool toDsp)
{
    addr = DSP_MEM_ADDR_TO_CPU(addr);
    len = DSP_MEM_ADDR_TO_CPU(len < 1 ? 1 : len);
	
    int segBits = isCode ? _codeSegs : _dataSegs;
    int start = addr >> TWR_WRAM_BC_SLOT_SHIFT;
    int end = (addr + len - 1) >> TWR_WRAM_BC_SLOT_SHIFT;
    for (int i = start; i <= end; i++)
    {
        if (!(segBits & (1 << i)))
            continue;
		
        int slot = isCode ? _codeSlots[i] : _dataSlots[i];
        if (isCode)
            twr_mapWramBSlot(slot, toDsp ? TWR_WRAM_B_SLOT_MASTER_DSP_CODE : TWR_WRAM_B_SLOT_MASTER_ARM9, toDsp ? i : slot, true);
        else
            twr_mapWramCSlot(slot, toDsp ? TWR_WRAM_C_SLOT_MASTER_DSP_DATA : TWR_WRAM_C_SLOT_MASTER_ARM9, toDsp ? i : slot, true);
    }
}

void TDSP_LoadBinary(const dsp_dsp1_t* dsp1)
{
    if(dsp1->header.magic != DSP_DSP1_MAGIC)
        return;

    _slotB = 0xFF;//dsp1->header.memoryLayout & 0xFF;
    _slotC = 0xFF;//(dsp1->header.memoryLayout >> 8) & 0xFF;

    _codeSegs = 0xFF;
    _dataSegs = 0xFF;

    for(int i = 0; i < TWR_WRAM_BC_SLOT_COUNT; i++)
    {
        _codeSlots[i] = i;
        _dataSlots[i] = i;
        twr_mapWramBSlot(i, TWR_WRAM_B_SLOT_MASTER_ARM9, i, true);
        u32* slot = (u32*)(twr_getBlockAddress(TWR_WRAM_BLOCK_B) + i * TWR_WRAM_BC_SLOT_SIZE);
        for(int j = 0; j < (TWR_WRAM_BC_SLOT_SIZE >> 2); j++)
            *slot++ = 0;
        twr_mapWramCSlot(i, TWR_WRAM_C_SLOT_MASTER_ARM9, i, true);
        slot = (u32*)(twr_getBlockAddress(TWR_WRAM_BLOCK_C) + i * TWR_WRAM_BC_SLOT_SIZE);
        for(int j = 0; j < (TWR_WRAM_BC_SLOT_SIZE >> 2); j++)
            *slot++ = 0;
    }

    //if(dsp1->header.flags & DSP_DSP1_FLAG_SYNC_LOAD)
    //    _flags |= DSP_PROCESS_FLAG_SYNC_LOAD;

    for(int i = 0; i < dsp1->header.nrSegments; i++)
    {
        bool isCode = dsp1->segments[i].segmentType != DSP_DSP1_SEG_TYPE_DATA;
		u16* dst = (u16*)DspToArm9Address(isCode, dsp1->segments[i].address);
		int size = ((dsp1->segments[i].size + 1) >> 1) << 1;
        memcpy(dst, (u16*)(((u8*)dsp1) + dsp1->segments[i].offset), size);
		DC_FlushRange(dst, size);
    }

    TDSP_SetMemoryMapping(true, 0, (TWR_WRAM_BC_SLOT_SIZE * TWR_WRAM_BC_SLOT_COUNT) >> 1, true);
    TDSP_SetMemoryMapping(false, 0, (TWR_WRAM_BC_SLOT_SIZE * TWR_WRAM_BC_SLOT_COUNT) >> 1, true);

    dsp_powerOn();
	dsp_setCoreResetOff(0);
	dsp_setSemaphoreMask(0);
}

// static DspProcess* sDspProcess = NULL;

// void DspProcess::DspIrqHandler()
// {
//     if(sDspProcess)
//         sDspProcess->HandleDspIrq();
// }

// void DspProcess::HandleDspIrq()
// {    
//     while(true)
//     {
//         u32 sources = (REG_DSP_SEM | (((REG_DSP_PSTS >> DSP_PCFG_IE_REP_SHIFT) & 7) << 16)) & _callbackSources;
//         if(!sources)
//             break;
//         while(sources)
//         {
//             int idx = MATH_CountTrailingZeros(sources);
//             sources &= ~_callbackGroups[idx];
//             _callbackFuncs[idx](_callbackArgs[idx]);
//         }
//     }
// }

#if 0
DspProcess::DspProcess(bool forceDspSyncLoad)
    : _slotB(0), _slotC(0), _codeSegs(0), _dataSegs(0), _flags(forceDspSyncLoad ? DSP_PROCESS_FLAG_SYNC_LOAD : 0)
{
    for(int i = 0; i < TWR_WRAM_BC_SLOT_COUNT; i++)
    {
        _codeSlots[i] = 0xFF;
        _dataSlots[i] = 0xFF;
    }
    // for(int i = 0; i < DSP_PROCESS_CALLBACK_COUNT; i++)
    // {
    //     _callbackFuncs[i] = NULL;
    //     _callbackArgs[i] = NULL;
    //     _callbackGroups[i] = 0;
    // }
}

bool DspProcess::SetMemoryMapping(bool isCode, u32 addr, u32 len, bool toDsp)
{
    addr = DSP_MEM_ADDR_TO_CPU(addr);
    len = DSP_MEM_ADDR_TO_CPU(len < 1 ? 1 : len);
    int segBits = isCode ? _codeSegs : _dataSegs;
    int start = addr >> TWR_WRAM_BC_SLOT_SHIFT;
    int end = (addr + len - 1) >> TWR_WRAM_BC_SLOT_SHIFT;
    for(int i = start; i <= end; i++)
    {
        if(!(segBits & (1 << i)))
            continue;
        int slot = isCode ? _codeSlots[i] : _dataSlots[i];
        if(isCode)
            twr_mapWramBSlot(slot, toDsp ? TWR_WRAM_B_SLOT_MASTER_DSP_CODE : TWR_WRAM_B_SLOT_MASTER_ARM9, toDsp ? i : slot, true);
        else
            twr_mapWramCSlot(slot, toDsp ? TWR_WRAM_C_SLOT_MASTER_DSP_DATA : TWR_WRAM_C_SLOT_MASTER_ARM9, toDsp ? i : slot, true);
    }
    return true;
}

bool DspProcess::Execute()
{
    //OSIntrMode irq = OS_DisableInterrupts();
	//{
        //sDspProcess = this;
        //dsp_initPipe();
        //OS_SetIrqFunction(OS_IE_DSP, DspProcess::DspIrqHandler);
        //SetCallback(DSP_PROCESS_CALLBACK_SEMAPHORE(15) | DSP_PROCESS_CALLBACK_REPLY(DSP_PIPE_CMD_REG), dsp_pipeIrqCallback, NULL);
        //OS_EnableIrqMask(OS_IE_DSP);
		dsp_powerOn();
		dsp_setCoreResetOff(0);//_callbackSources >> 16);
		dsp_setSemaphoreMask(0);
        //SetupCallbacks();
		//needed for some modules
		if(_flags & DSP_PROCESS_FLAG_SYNC_LOAD)
			for(int i = 0; i < 3; i++)
				while(dsp_receiveData(i) != 1);
        //DspProcess::DspIrqHandler();
	//}
	//OS_RestoreInterrupts(irq);
    return true;
}

bool DspProcess::ExecuteDsp1(const dsp_dsp1_t* dsp1)
{
    // if(sDspProcess)
    //     return false;

    if(dsp1->header.magic != DSP_DSP1_MAGIC)
        return false;

    _slotB = 0xFF;//dsp1->header.memoryLayout & 0xFF;
    _slotC = 0xFF;//(dsp1->header.memoryLayout >> 8) & 0xFF;

    _codeSegs = 0xFF;
    _dataSegs = 0xFF;

    for(int i = 0; i < TWR_WRAM_BC_SLOT_COUNT; i++)
    {
        _codeSlots[i] = i;
        _dataSlots[i] = i;
        twr_mapWramBSlot(i, TWR_WRAM_B_SLOT_MASTER_ARM9, i, true);
        u32* slot = (u32*)(twr_getBlockAddress(TWR_WRAM_BLOCK_B) + i * TWR_WRAM_BC_SLOT_SIZE);
        for(int j = 0; j < (TWR_WRAM_BC_SLOT_SIZE >> 2); j++)
            *slot++ = 0;
        twr_mapWramCSlot(i, TWR_WRAM_C_SLOT_MASTER_ARM9, i, true);
        slot = (u32*)(twr_getBlockAddress(TWR_WRAM_BLOCK_C) + i * TWR_WRAM_BC_SLOT_SIZE);
        for(int j = 0; j < (TWR_WRAM_BC_SLOT_SIZE >> 2); j++)
            *slot++ = 0;
    }

    if(dsp1->header.flags & DSP_DSP1_FLAG_SYNC_LOAD)
        _flags |= DSP_PROCESS_FLAG_SYNC_LOAD;

    for(int i = 0; i < dsp1->header.nrSegments; i++)
    {
        bool isCode = dsp1->segments[i].segmentType != DSP_DSP1_SEG_TYPE_DATA;
        arm9_memcpy16((u16*)DspToArm9Address(isCode, dsp1->segments[i].address), (u16*)(((u8*)dsp1) + dsp1->segments[i].offset), (dsp1->segments[i].size + 1) >> 1);
    }

    SetMemoryMapping(true, 0, (TWR_WRAM_BC_SLOT_SIZE * TWR_WRAM_BC_SLOT_COUNT) >> 1, true);
    SetMemoryMapping(false, 0, (TWR_WRAM_BC_SLOT_SIZE * TWR_WRAM_BC_SLOT_COUNT) >> 1, true);

    return Execute();
}

// void DspProcess::SetCallback(u32 sources, dsp_process_irq_callback_t func, void* arg)
// {
//     OSIntrMode irq = OS_DisableInterrupts();
// 	{
//         for(int i = 0; i < DSP_PROCESS_CALLBACK_COUNT; i++)
//         {
//             if(!(sources & (1 << i)))
//                 continue;
//             _callbackFuncs[i] = func;
//             _callbackArgs[i] = arg;
//             _callbackGroups[i] = sources;
//         }
//         if(func)
//         {
//             REG_DSP_PCFG |= ((sources >> 16) & 7) << DSP_PCFG_IE_REP_SHIFT;
//             REG_DSP_PMASK &= ~(sources & 0xFFFF);
//             _callbackSources |= sources;
//         }
//         else
//         {
//             REG_DSP_PCFG &= ~(((sources >> 16) & 7) << DSP_PCFG_IE_REP_SHIFT);
//             REG_DSP_PMASK |= sources & 0xFFFF;
//             _callbackSources &= ~sources;
//         }        
// 	}
// 	OS_RestoreInterrupts(irq);
// }
#endif