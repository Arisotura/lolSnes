.section .text

.global PPU_DoDrawScanline


// r0 = output buffer
// r1 = Y pos.
// r7 = BG num.
DrawBG_8x8_4bpp:
	//


// a0l = state struct.
// a1l = output buffer
// b0l = Y pos.
PPU_DoDrawScanline:
	push r0
	push r1
	push r2
	
	mov a1l, r0
	mov a0l, r2
	addv 0x1B, r2
	mov 0x100, r1
	bkrep r1, vorpi
	mov [r2++], a0l
	or 0x8000, a0
	mov a0l, [r0++]
vorpi:
	
	pop r2
	pop r1
	pop r0
	ret always




.global PPU_test
.global PPU_test2
PPU_test:
	push r0
	push r1
	
	mov a0l, r0
	mov 0x83E0, a0l
	mov 0x100, r1
	bkrep r1, vorp
	mov a0l, [r0++]
vorp:
	
	pop r1
	pop r0
	ret always
	
	
PPU_test2:
	push r0
	push r1
	
	mov a0l, r0
	mov 0xFC00, a0h
	mov 0x83E0, a0
	mov 0x80, r1
	bkrep r1, vorp2
	mov a0, [arrn0+ars1]
vorp2:
	
	pop r1
	pop r0
	ret always
