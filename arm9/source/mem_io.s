
.arm
.align 4

#include "cpu.inc"

.section    .itcm, "aw", %progbits

write16msg:
	.ascii "IOWrite16 %06X %04X\n\0"

.align 4
.global Mem_IORead8
.global Mem_IORead16
.global Mem_IOWrite8
.global Mem_IOWrite16

Mem_IORead8:
	and r1, r0, #0xFF00
	and r0, r0, #0xFF
	
	cmp r1, #0x2100
	beq PPU_Read8
	
	cmp r1, #0x4200
	beq Mem_GIORead8
	
	mov r0, #0
	bx lr
	
Mem_IORead16:
	and r1, r0, #0xFF00
	and r0, r0, #0xFF
	
	cmp r1, #0x2100
	beq PPU_Read16
	
	cmp r1, #0x4200
	beq Mem_GIORead16
	
	mov r0, #0
	bx lr
	
Mem_IOWrite8:
	and r2, r0, #0xFF00
	and r0, r0, #0xFF
	
	cmp r2, #0x2100
	beq PPU_Write8
	
	cmp r2, #0x4200
	bxne lr
	cmp r0, #0x00
	bne Mem_GIOWrite8
	tst r1, #0x80
	bicne snesP, snesP, #flagI2
	orreq snesP, snesP, #flagI2
	bx lr
	
	bx lr
	
Mem_IOWrite16:
	and r2, r0, #0xFF00
	and r0, r0, #0xFF
	
	cmp r2, #0x2100
	beq PPU_Write16
	
	cmp r2, #0x4200
	beq Mem_GIOWrite16
	
	bx lr
