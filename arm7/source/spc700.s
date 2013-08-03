
.arm

@ --- TODO --------------------------------------------------------------------
@
@ search the code for 'todo' for more
@ -----------------------------------------------------------------------------

#include "spc700.inc"

.align 4
.global SPC_Regs
SPC_Regs:
	.long 0,0,0,0,0,0,0,0

.global SPC_Memory
SPC_Memory:
	.rept 0xFFC0
	.byte 0
	.endr
	.byte 0xCD,0xEF,0xBD,0xE8,0x00,0xC6,0x1D,0xD0
	.byte 0xFC,0x8F,0xAA,0xF4,0x8F,0xBB,0xF5,0x78
	.byte 0xCC,0xF4,0xD0,0xFB,0x2F,0x19,0xEB,0xF4
	.byte 0xD0,0xFC,0x7E,0xF4,0xD0,0x0B,0xE4,0xF5
	.byte 0xCB,0xF4,0xD7,0x00,0xFC,0xD0,0xF3,0xAB
	.byte 0x01,0x10,0xEF,0x7E,0xF4,0x10,0xEB,0xBA
	.byte 0xF6,0xDA,0x00,0xBA,0xF4,0xC4,0xF4,0xDD
	.byte 0x5D,0xD0,0xDB,0x1F,0x00,0x00,0xC0,0xFF
	
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

.macro Prefetch8
	ldrb r0, [memory, spcPC, lsr #0x10]
	add spcPC, spcPC, #0x10000
.endm

.macro Prefetch16
	ldrb r0, [memory, spcPC, lsr #0x10]
	add spcPC, spcPC, #0x10000
	ldrb r3, [memory, spcPC, lsr #0x10]
	add spcPC, spcPC, #0x10000
	orr r0, r0, r3, lsl #0x8
.endm

@ --- Opcode tables -----------------------------------------------------------

OpTableStart:
	.long OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK	@0
	.long OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK
	.long OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK	@1
	.long OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK
	.long OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK	@2
	.long OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK
	.long OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK	@3
	.long OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK
	.long OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK	@4
	.long OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK
	.long OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK	@5
	.long OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK
	.long OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK	@6
	.long OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK
	.long OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK	@7
	.long OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK
	.long OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK	@8
	.long OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK
	.long OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK	@9
	.long OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK
	.long OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK	@A
	.long OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK
	.long OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK	@B
	.long OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_MOV_SP_X, OP_UNK, OP_UNK
	.long OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK	@C
	.long OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_MOV_X_Imm, OP_UNK, OP_UNK
	.long OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK	@D
	.long OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK
	.long OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK	@E
	.long OP_MOV_A_Imm, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK
	.long OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK	@F
	.long OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK

@ --- Misc. functions ---------------------------------------------------------

.global SPC_Reset
.global SPC_Run

.macro LoadRegs
	ldr r0, =SPC_Regs
	ldmia r0, {r4-r11}
.endm

.macro StoreRegs
	ldr r0, =SPC_Regs
	stmia r0, {r4-r11}
.endm


.macro SetPC src=r0
	mov \src, \src, lsl #0x10
	mov spcPC, spcPC, lsl #0x10
	orr spcPC, \src, spcPC, lsr #0x10
.endm


@ add cycles
.macro AddCycles num, cond=
	sub\cond spcCycles, spcCycles, #\num
.endm


SPC_Reset:
	stmdb sp!, {lr}
	
	mov spcA, #0
	mov spcX, #0
	mov spcY, #0
	mov spcSP, #0x100
	mov spcPSW, #0	@ we'll do PC later
	
	ldr memory, =SPC_Memory
	ldr opTable, =OpTableStart
	
	ldr r0, =vec_Reset
	orr spcPC, spcPC, r0, lsl #0x10
	
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
			
emuloop:
			Prefetch8
			ldr r0, [opTable, r0, lsl #0x2]
			bx r0
op_return:

			cmp spcCycles, #1
			bge emuloop
		
		@ we ran a frame -- sync with main CPU here
			
		swi #0x50000
		b frameloop
		
.ltorg
	
@ --- Addressing modes --------------------------------------------------------

@ todo

@ --- Unknown opcode ----------------------------------------------------------

OP_UNK:
	@swi #0xBEEF
	b OP_UNK
	
@ --- MOV ---------------------------------------------------------------------

OP_MOV_A_Imm:
	Prefetch8
	movs spcA, r0
	orreq spcPSW, spcPSW, #flagZ
	bicne spcPSW, spcPSW, #flagZ
	tst spcA, #0x80
	orrne spcPSW, spcPSW, #flagN
	biceq spcPSW, spcPSW, #flagN
	AddCycles 2
	b op_return

OP_MOV_X_Imm:
	Prefetch8
	movs spcX, r0
	orreq spcPSW, spcPSW, #flagZ
	bicne spcPSW, spcPSW, #flagZ
	tst spcX, #0x80
	orrne spcPSW, spcPSW, #flagN
	biceq spcPSW, spcPSW, #flagN
	AddCycles 2
	b op_return
	
OP_MOV_SP_X:
	orr spcSP, spcX, #0x100
	AddCycles 2
	b op_return
