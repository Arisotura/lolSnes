
.arm

@ --- TODO --------------------------------------------------------------------
@ * emulate dummy reads (trigger read-sensitive IO ports)
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

.macro MemRead8 addr=r0
	bic r3, \addr, #0x000F
	cmp r3, #0x00F0
	ldrneb r0, [memory, \addr]
	.ifnc \addr, r0
		moveq r0, \addr
	.endif
	bleq SPC_IORead8
.endm

.macro MemRead16 addr=r0
	bic r3, \addr, #0x000F
	cmp r3, #0x00F0
	addne r3, memory, \addr
	ldrneb r0, [r3]
	ldrneb r3, [r3, #0x1]
	orrne r0, r0, r3, lsl #0x8
	.ifnc \addr, r0
		moveq r0, \addr
	.endif
	bleq SPC_IORead16
.endm

.macro MemWrite8 addr=r0, val=r1
	bic r3, \addr, #0x000F
	ldr r12, =0xFFC0
	cmp r3, r12
	beq 1f
	cmp r3, #0x00F0
	strneb \val, [memory, \addr]
	.ifnc \addr, r0
		moveq r0, \addr
	.endif
	.ifnc \val, r1
		moveq r1, \val
	.endif
	bleq SPC_IOWrite8
1:
.endm

.macro MemWrite16 addr=r0, val=r1
	bic r3, \addr, #0x000F
	ldr r12, =0xFFC0
	cmp r3, r12
	beq 1f
	cmp r3, #0x00F0
	addne r3, memory, \addr
	strneb \val, [r3]
	movne \val, \val, lsr #0x8
	strneb \val, [r3, #0x1]
	.ifnc \addr, r0
		moveq r0, \addr
	.endif
	.ifnc \val, r1
		moveq r1, \val
	.endif
	bleq SPC_IOWrite16
1:
.endm

@ --- Stack read/write --------------------------------------------------------
@ assume they always happen in SPC RAM, page 1
@ increment/decrement SP as well

.macro StackRead8
	add spcSP, spcSP, #1
	orr spcSP, spcSP, #0x100
	ldrb r0, [memory, spcSP]
.endm

.macro StackRead16
	add spcSP, spcSP, #2
	orr spcSP, spcSP, #0x100
	add r12, memory, spcSP
	ldrb r0, [r12, #-1]
	ldrb r3, [r12]
	orr r0, r0, r3, lsl #0x8
.endm

.macro StackWrite8 src=r0
	strb \src, [memory, spcSP]
	sub spcSP, spcSP, #1
	orr spcSP, spcSP, #0x100
.endm

.macro StackWrite16 src=r0
	add r12, memory, spcSP
	strb \src, [r12, #-1]
	mov \src, \src, lsr #0x8
	strb \src, [r12]
	sub spcSP, spcSP, #2
	orr spcSP, spcSP, #0x100
.endm

@ --- Prefetch ----------------------------------------------------------------

.macro Prefetch8 dst=r0
	ldrb \dst, [memory, spcPC, lsr #0x10]
	add spcPC, spcPC, #0x10000
.endm

.macro Prefetch16 dst=r0
	ldrb \dst, [memory, spcPC, lsr #0x10]
	add spcPC, spcPC, #0x10000
	ldrb r3, [memory, spcPC, lsr #0x10]
	add spcPC, spcPC, #0x10000
	orr \dst, \dst, r3, lsl #0x8
.endm

@ --- Opcode tables -----------------------------------------------------------

OpTableStart:
	.long OP_NOP, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_BRK	@0
	.long OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK
	.long OP_BPL, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK	@1
	.long OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_DEC_X, OP_CMP_X_mImm, OP_JMP_a_X
	.long OP_CLRP, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK	@2
	.long OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_BRA
	.long OP_BMI, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_CMP_X_DP, OP_UNK	@3
	.long OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_INC_X, OP_UNK, OP_CALL
	.long OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK	@4
	.long OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK
	.long OP_BVC, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK	@5
	.long OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_MOV_X_A, OP_CMP_Y_mImm, OP_JMP_a
	.long OP_CLRC, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK	@6
	.long OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_RET
	.long OP_BVS, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK	@7
	.long OP_CMP_DP_Imm, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_CMP_Y_DP, OP_UNK
	.long OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK	@8
	.long OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_MOV_Y_Imm, OP_UNK, OP_MOV_DP_Imm
	.long OP_BCC, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK	@9
	.long OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_DEC_A, OP_UNK, OP_UNK, OP_UNK
	.long OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK	@A
	.long OP_UNK, OP_UNK, OP_UNK, OP_INC_DP, OP_UNK, OP_CMP_Y_Imm, OP_UNK, OP_MOV_mX_A_Inc
	.long OP_BCS, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_UNK	@B
	.long OP_UNK, OP_UNK, OP_MOVW_YA_DP, OP_UNK, OP_INC_A, OP_MOV_SP_X, OP_UNK, OP_UNK
	.long OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_MOV_DP_A, OP_MOV_Imm_A, OP_MOV_mX_A, OP_UNK	@C
	.long OP_CMP_X_Imm, OP_MOV_Imm_X, OP_UNK, OP_MOV_DP_Y, OP_MOV_Imm_Y, OP_MOV_X_Imm, OP_UNK, OP_UNK
	.long OP_BNE, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_MOV_lmX_A, OP_MOV_lmY_A, OP_MOV_m_Y_A	@D
	.long OP_MOV_DP_X, OP_UNK, OP_MOVW_DP_YA, OP_UNK, OP_DEC_Y, OP_MOV_A_Y, OP_UNK, OP_UNK
	.long OP_CLRV, OP_UNK, OP_UNK, OP_UNK, OP_MOV_A_DP, OP_UNK, OP_UNK, OP_UNK	@E
	.long OP_MOV_A_Imm, OP_MOV_X_lm, OP_UNK, OP_MOV_Y_DP, OP_MOV_Y_lm, OP_UNK, OP_UNK, OP_UNK
	.long OP_BEQ, OP_UNK, OP_UNK, OP_UNK, OP_UNK, OP_MOV_A_lmX, OP_MOV_A_lmY, OP_UNK	@F
	.long OP_MOV_X_DP, OP_UNK, OP_UNK, OP_UNK, OP_INC_Y, OP_MOV_Y_A, OP_UNK, OP_UNK

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
	ldr r0, =0x04000210
	ldr r1, [r0]
	orr r1, r1, #0x00010000
	str r1, [r0]
	
frameloop:
		@ldr r0, =0x42AB
		mov r0, #0x20
		add spcCycles, spcCycles, r0
			
emuloop:
	@mov r14, spcPC
			Prefetch8
			ldr r0, [opTable, r0, lsl #0x2]
			bx r0
op_return:

			@mov r0, spcPC, lsr #0x10
			@cmp r0, #0x0500
		@lolz:
		@	blt lolz

			cmp spcCycles, #1
			bge emuloop
		
		@ we ran a frame -- sync with main CPU here
		@ use swi 4
			
		@swi #0x50000
		mov r0, #1
		mov r1, #0x00010000
		swi #0x40000
		@ldr r0, =0x04000210
		@ldr r1, [r0]
		@orr r1, r1, #0x00010000
		@str r1, [r0]
		b frameloop
		
.ltorg
	
@ --- Addressing modes --------------------------------------------------------

.macro GetAddr_Imm dst=r0
	Prefetch16 \dst
.endm

.macro GetOp_Imm dst=r0
	Prefetch8 \dst
.endm

.macro GetAddr_DP dst=r0
	Prefetch8 \dst
	tst spcPSW, #flagP
	orrne \dst, \dst, #0x100
.endm

.macro GetOp_DP
	GetAddr_DP r0
	MemRead8 r0
.endm

.macro GetAddr_mX
	tst spcPSW, #flagP
	moveq r0, spcX
	orrne r0, spcX, #0x100
.endm

.macro GetAddr_m_Y
	Prefetch8
	MemRead16
	add r0, r0, spcY
.endm

.macro GetOp_m_Y
	GetAddr_m_Y
	MemRead8
.endm

@ --- Unknown opcode ----------------------------------------------------------

OP_UNK:
	mov r0, spcPC, lsr #0x10
	sub r0, r0, #1
	MemRead8
blarg2:
	b blarg2
	
@ --- Branch ------------------------------------------------------------------

.macro BRANCH cb, cnb=0, flag=0, cond=0
	.ifne \flag
		tst spcPSW, #\flag
		.ifeq \cond
			beq 1f
		.else
			bne 1f
		.endif
			add spcPC, spcPC, #0x10000
			AddCycles \cnb
			b op_return
1:
	.endif
	GetOp_Imm
	mov r0, r0, lsl #0x18
	add spcPC, spcPC, r0, asr #0x8
	AddCycles \cb
	b op_return
.endm

OP_BRA:
	BRANCH 4
	
OP_BCC:
	BRANCH 4, 2, flagC, 0
	
OP_BCS:
	BRANCH 4, 2, flagC, 1
	
OP_BEQ:
	BRANCH 4, 2, flagZ, 1
	
OP_BMI:
	BRANCH 4, 2, flagN, 1

OP_BNE:
	BRANCH 4, 2, flagZ, 0
	
OP_BPL:
	BRANCH 4, 2, flagN, 0
	
OP_BVC:
	BRANCH 4, 2, flagV, 0
	
OP_BVS:
	BRANCH 4, 2, flagV, 1
	
@ --- BRK ---------------------------------------------------------------------

OP_BRK:
	ldr r0, =0xDEADBEEF
blarg:
	b blarg
	
@ --- CALL --------------------------------------------------------------------

OP_CALL:
	Prefetch16
	mov r1, spcPC, lsr #0x10
	StackWrite16 r1
	SetPC
	AddCycles 8
	b op_return
	
@ --- CLRx --------------------------------------------------------------------

OP_CLRC:
	bic spcPSW, spcPSW, #flagC
	AddCycles 2
	b op_return
	
OP_CLRP:
	bic spcPSW, spcPSW, #flagP
	AddCycles 2
	b op_return
	
OP_CLRV:
	bic spcPSW, spcPSW, #flagVH
	AddCycles 2
	b op_return

@ --- CMP ---------------------------------------------------------------------

.macro DO_CMP a, b
	cmp \a, \b
	bic spcPSW, spcPSW, #flagNZC
	orreq spcPSW, spcPSW, #flagZ
	orrge spcPSW, spcPSW, #flagC
	orrlt spcPSW, spcPSW, #flagN
.endm

OP_CMP_DP_Imm:
	GetOp_Imm r1
	GetOp_DP
	DO_CMP r0, r1
	AddCycles 5
	b op_return
	
OP_CMP_X_Imm:
	GetOp_Imm
	DO_CMP spcX, r0
	AddCycles 2
	b op_return
	
OP_CMP_X_DP:
	GetOp_DP
	DO_CMP spcX, r0
	AddCycles 3
	b op_return
	
OP_CMP_X_mImm:
	GetAddr_Imm
	MemRead8
	DO_CMP spcX, r0
	AddCycles 4
	b op_return
	
OP_CMP_Y_Imm:
	GetOp_Imm
	DO_CMP spcY, r0
	AddCycles 2
	b op_return
	
OP_CMP_Y_DP:
	GetOp_DP
	DO_CMP spcY, r0
	AddCycles 3
	b op_return
	
OP_CMP_Y_mImm:
	GetAddr_Imm
	MemRead8
	DO_CMP spcY, r0
	AddCycles 4
	b op_return
	
@ --- DEC ---------------------------------------------------------------------

.macro DO_DEC dst
	sub \dst, \dst, #1
	ands \dst, \dst, #0xFF
	orreq spcPSW, spcPSW, #flagZ
	bicne spcPSW, spcPSW, #flagZ
	tst \dst, #0x80
	orrne spcPSW, spcPSW, #flagN
	biceq spcPSW, spcPSW, #flagN
	AddCycles 2
	b op_return
.endm

OP_DEC_A:
	DO_DEC spcA

OP_DEC_X:
	DO_DEC spcX
	
OP_DEC_Y:
	DO_DEC spcY
	
@ --- INC ---------------------------------------------------------------------

.macro DO_INC dst
	add \dst, \dst, #1
	ands \dst, \dst, #0xFF
	orreq spcPSW, spcPSW, #flagZ
	bicne spcPSW, spcPSW, #flagZ
	tst \dst, #0x80
	orrne spcPSW, spcPSW, #flagN
	biceq spcPSW, spcPSW, #flagN
.endm

OP_INC_A:
	DO_INC spcA
	AddCycles 2
	b op_return
	
OP_INC_DP:
	GetAddr_DP r2
	MemRead8 r2
	DO_INC r0
	mov r1, r0
	mov r0, r2
	MemWrite8
	AddCycles 4
	b op_return

OP_INC_X:
	DO_INC spcX
	AddCycles 2
	b op_return

OP_INC_Y:
	DO_INC spcY
	AddCycles 2
	b op_return
	
@ --- JMP ---------------------------------------------------------------------

OP_JMP_a_X:
	Prefetch16
	add r0, r0, spcX
	MemRead16
	SetPC r0
	AddCycles 6
	b op_return
	
OP_JMP_a:
	Prefetch16
	SetPC r0
	AddCycles 3
	b op_return
	
@ --- MOV ---------------------------------------------------------------------

.macro DO_MOV dst, src
	movs \dst, \src
	orreq spcPSW, spcPSW, #flagZ
	bicne spcPSW, spcPSW, #flagZ
	tst \dst, #0x80
	orrne spcPSW, spcPSW, #flagN
	biceq spcPSW, spcPSW, #flagN
.endm

OP_MOV_A_DP:
	GetOp_DP
	DO_MOV spcA, r0
	AddCycles 3
	b op_return

OP_MOV_A_Imm:
	GetOp_Imm
	DO_MOV spcA, r0
	AddCycles 2
	b op_return
	
OP_MOV_A_lmX:
	GetAddr_Imm
	add r0, r0, spcX
	MemRead16
	DO_MOV spcA, r0
	AddCycles 5
	b op_return
	
OP_MOV_A_lmY:
	GetAddr_Imm
	add r0, r0, spcY
	MemRead16
	DO_MOV spcA, r0
	AddCycles 5
	b op_return
	
OP_MOV_A_Y:
	DO_MOV spcA, spcY
	AddCycles 2
	b op_return
	
OP_MOV_DP_Imm:
	GetOp_Imm r1
	GetAddr_DP r0
	MemWrite8
	AddCycles 5
	b op_return
	
OP_MOV_DP_A:
	GetAddr_DP
	mov r1, spcA
	MemWrite8
	AddCycles 4
	b op_return
	
OP_MOV_DP_X:
	GetAddr_DP
	mov r1, spcX
	MemWrite8
	AddCycles 4
	b op_return
	
OP_MOV_DP_Y:
	GetAddr_DP
	mov r1, spcY
	MemWrite8
	AddCycles 4
	b op_return
	
OP_MOV_lmX_A:
	GetAddr_Imm
	add r0, r0, spcX
	mov r1, spcA
	MemWrite8
	AddCycles 6
	b op_return
	
OP_MOV_lmY_A:
	GetAddr_Imm
	add r0, r0, spcY
	mov r1, spcA
	MemWrite8
	AddCycles 6
	b op_return
	
OP_MOV_Imm_A:
	GetAddr_Imm
	mov r1, spcA
	MemWrite8
	AddCycles 5
	b op_return
	
OP_MOV_Imm_X:
	GetAddr_Imm
	mov r1, spcX
	MemWrite8
	AddCycles 5
	b op_return
	
OP_MOV_Imm_Y:
	GetAddr_Imm
	mov r1, spcY
	MemWrite8
	AddCycles 5
	b op_return
	
OP_MOV_mX_A:
	GetAddr_mX
	mov r1, spcA
	MemWrite8
	AddCycles 4
	b op_return
	
OP_MOV_mX_A_Inc:
	GetAddr_mX
	mov r1, spcA
	MemWrite8
	add spcX, spcX, #1
	and spcX, spcX, #0xFF
	AddCycles 4
	b op_return
	
OP_MOV_m_Y_A:
	GetAddr_m_Y
	mov r1, spcA
	MemWrite8
	AddCycles 7
	b op_return
	
OP_MOV_X_A:
	DO_MOV spcX, spcA
	AddCycles 2
	b op_return
	
OP_MOV_X_DP:
	GetOp_DP
	DO_MOV spcX, r0
	AddCycles 3
	b op_return
	
OP_MOV_X_Imm:
	GetOp_Imm
	DO_MOV spcX, r0
	AddCycles 2
	b op_return
	
OP_MOV_X_lm:
	GetAddr_DP
	MemRead8
	DO_MOV spcX, r0
	AddCycles 4
	b op_return
	
OP_MOV_Y_A:
	DO_MOV spcY, spcA
	AddCycles 2
	b op_return
	
OP_MOV_Y_DP:
	GetOp_DP
	DO_MOV spcY, r0
	AddCycles 3
	b op_return
	
OP_MOV_Y_Imm:
	GetOp_Imm
	DO_MOV spcY, r0
	AddCycles 2
	b op_return
	
OP_MOV_Y_lm:
	GetAddr_DP
	MemRead8
	DO_MOV spcY, r0
	AddCycles 4
	b op_return
	
OP_MOV_SP_X:
	orr spcSP, spcX, #0x100
	AddCycles 2
	b op_return
	
@ --- MOVW --------------------------------------------------------------------

OP_MOVW_YA_DP:
	GetAddr_DP
	MemRead16
	cmp r0, #0
	orreq spcPSW, spcPSW, #flagZ
	bicne spcPSW, spcPSW, #flagZ
	tst r0, #0x8000
	orrne spcPSW, spcPSW, #flagN
	biceq spcPSW, spcPSW, #flagN
	and spcA, r0, #0xFF
	mov spcY, r0, lsr #0x8
	AddCycles 5
	b op_return

OP_MOVW_DP_YA:
	GetAddr_DP
	orr r1, spcA, spcY, lsl #0x8
	MemWrite16
	AddCycles 5
	b op_return
	
@ --- NOP ---------------------------------------------------------------------

OP_NOP:
	AddCycles 2
	b op_return
	
@ --- RET ---------------------------------------------------------------------

OP_RET:
	StackRead16
	SetPC
	AddCycles 5
	b op_return
