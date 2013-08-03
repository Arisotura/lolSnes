
.arm

@ --- TODO --------------------------------------------------------------------
@
@ search the code for 'todo' for more
@ -----------------------------------------------------------------------------

#include "spc700.inc"

.align 4
.global CPU_Regs
CPU_Regs:
	.long 0,0,0,0,0,0,0,0
	
.equ vec_Reset, 0xFFC0
	
@ --- General purpose read/write ----------------------------------------------

.macro MemRead8
@ TODO
.endm

.macro MemRead16
@ TODO
.endm

.macro MemRead24
@ TODO (is it even needed?)
.endm

.macro MemWrite8
@ TODO
.endm

.macro MemWrite16
@ TODO
.endm

@ --- Stack read/write --------------------------------------------------------
@ assume they always happen in SPC RAM, page 1
@ increment/decrement SP as well

.macro StackRead8
@ TODO
.endm

.macro StackRead16
@ TODO
.endm

.macro StackWrite8 src=r0
@ TODO
.endm

.macro StackWrite16 src=r0
@ TODO
.endm

@ --- Prefetch ----------------------------------------------------------------
@ assume that prefetches always occur in system RAM or cached ROM
@ (anything that modifies PBR will also update the ROM caches)
@ OpcodePrefetch8 used to prefetch the opcode
@ other prefetches are sequential (to be called right after the opcode prefetch)

.macro OpcodePrefetch8
@ TODO
.endm

.macro Prefetch8
@ TODO
.endm

.macro Prefetch16
@ TODO
.endm

.macro Prefetch24
@ TODO
.endm

@ --- Opcode tables -----------------------------------------------------------

OpTableStart:
@ TODO

@ --- Misc. functions ---------------------------------------------------------

.global SPC_Reset
.global SPC_Run

.macro LoadRegs
	ldr r0, =CPU_Regs
	ldmia r0, {r4-r11}
.endm

.macro StoreRegs
	ldr r0, =CPU_Regs
	stmia r0, {r4-r11}
.endm


.macro SetPC src=r0
	mov \src, \src, lsl #0x10
	mov spcPC, spcPC, lsl #0x10
	orr spcPC, \src, spcPC, lsr #0x10
.endm


@ add fast cycles (for CPU IO cycles)
.macro AddCycles num, cond=
	sub\cond spcCycles, spcCycles, #(\num * 0x60000)
.endm


SPC_Reset:
	stmdb sp!, {lr}
	bl Mem_Reset
	
	mov spcA, #0
	mov spcX, #0
	mov spcY, #0
	mov spcSP, #0
	mov spcPSW, #0	@ we'll do PC later
	
	@ TODO
	@ldr memoryMap, =Mem_PtrTable
	@SetOpcodeTable
	
	@ldr r0, =(ROM_Bank0 + 0x8000)
	@sub r0, r0, #vec_Reset
	@ldrh r0, [r0]
	ldr r0, =vec_Reset
	orr snesPC, snesPC, r0, lsl #0x10
	
	mov spcCycles, #0
	StoreRegs
	
	ldmia sp!, {lr}
	bx lr
	
@ --- Main loop ---------------------------------------------------------------
@ Synchronizes with main CPU once per frame
	
SPC_Run:
	LoadRegs
	
frameloop:
		ldr r0, =0x42AB
		add spcCycles, spcCycles, r0
		b emuloop
			
emuloop:
			@OpcodePrefetch8
			@ldr r0, [opTable, r0, lsl #0x2]
			@bx r0
			@ TODO get opcode and run it
op_return:

			cmp spcCycles, #1
			bge emuloop
		
		@ we ran a frame -- sync with main CPU here
			
		swi #0x50000
		b frameloop
		
.ltorg
	
@ --- Addressing modes --------------------------------------------------------

@ todo