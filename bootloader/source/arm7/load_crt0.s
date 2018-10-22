/*-----------------------------------------------------------------

 Copyright (C) 2005  Michael "Chishm" Chisholm

 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License
 as published by the Free Software Foundation; either version 2
 of the License, or (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

 If you use this code, please give due credit and email me about your
 project at chishm@hotmail.com
------------------------------------------------------------------*/

@---------------------------------------------------------------------------------
	.section ".init"
	.global _start
	.global storedFileCluster
	.global initDisc
	.global wantToPatchDLDI
	.global argStart
	.global argSize
	.global dsiSD
	.global saveFileCluster
	.global saveSize
	.global language
	.global dsiMode
	.global donorSdkVer
	.global patchMpuRegion
	.global patchMpuSize
	.global consoleModel
	.global loadingScreen
	.global romread_LED
	.global boostVram
	.global gameSoftReset
	.global asyncPrefetch
	.global soundFix
	.global logging
@---------------------------------------------------------------------------------
	.align	4
	.arm
@---------------------------------------------------------------------------------
_start:
@---------------------------------------------------------------------------------
	b	startUp

storedFileCluster:
	.word	0x0FFFFFFF		@ default BOOT.NDS
initDisc:
	.word	0x00000001		@ init the disc by default
wantToPatchDLDI:
	.word	0x00000000		@ by default patch the DLDI section of the loaded NDS
@ Used for passing arguments to the loaded app
argStart:
	.word	_end - _start
argSize:
	.word	0x00000000
dldiOffset:
	.word	0x00000000
dsiSD:
	.word	0
saveFileCluster:
	.word	0x00000000		@ .sav file
saveSize:
	.word	0x00000000		@ .sav file sive
language:
	.word	0x00000000
dsiMode:
	.word	0x00000000
donorSdkVer:
	.word	0x00000000		@ donor SDK version
patchMpuRegion:
	.word	0x00000000
patchMpuSize:
	.word	0x00000000
consoleModel:
	.word	0x00000000
loadingScreen:
	.word	0x00000000
romread_LED:
	.word	0x00000000
boostVram:
	.word	0x00000000
gameSoftReset:
	.word	0x00000000
asyncPrefetch:
	.word	0x00000000
soundFix:
	.word	0x00000000
logging:
	.word	0x00000000
cardengine_arm7_offset:
	.word   cardengine_arm7_bin - _start
cardengine_arm9_offset:
	.word   cardengine_arm9_bin - _start

startUp:
	mov	r0, #0x04000000
	mov	r1, #0
	str	r1, [r0,#0x208]		@ REG_IME
	str	r1, [r0,#0x210]		@ REG_IE
	str	r1, [r0,#0x218]		@ REG_AUXIE

	mov	r0, #0x12		@ Switch to IRQ Mode
	msr	cpsr, r0
	ldr	sp, =__sp_irq		@ Set IRQ stack

	mov	r0, #0x13		@ Switch to SVC Mode
	msr	cpsr, r0
	ldr	sp, =__sp_svc		@ Set SVC stack

	mov	r0, #0x1F		@ Switch to System Mode
	msr	cpsr, r0
	ldr	sp, =__sp_usr		@ Set user stack

	ldr	r0, =__bss_start	@ Clear BSS section to 0x00
	ldr	r1, =__bss_end
	sub	r1, r1, r0
	bl	ClearMem

@ Load ARM9 region into main RAM
	ldr	r1, =__arm9_source_start
	ldr	r2, =__arm9_start	
	ldr	r3, =__arm9_source_end
	sub	r3, r3, r1
	bl	CopyMem

@ Start ARM9 binary
	ldr	r0, =0x02FFFE24	
	ldr	r1, =_arm9_start
	str	r1, [r0]

	mov	r0, #0			@ int argc
	mov	r1, #0			@ char *argv[]
	ldr	r3, =arm7_main
	bl	_blx_r3_stub		@ jump to user code

	@ If the user ever returns, restart
	b	_start

@---------------------------------------------------------------------------------
_blx_r3_stub:
@---------------------------------------------------------------------------------
	bx	r3

@---------------------------------------------------------------------------------
@ Clear memory to 0x00 if length != 0
@  r0 = Start Address
@  r1 = Length
@---------------------------------------------------------------------------------
ClearMem:
@---------------------------------------------------------------------------------
	mov	r2, #3			@ Round down to nearest word boundary
	add	r1, r1, r2		@ Shouldnt be needed
	bics	r1, r1, r2		@ Clear 2 LSB (and set Z)
	bxeq	lr			@ Quit if copy size is 0

	mov	r2, #0
ClrLoop:
	stmia	r0!, {r2}
	subs	r1, r1, #4
	bne	ClrLoop
	bx	lr

@---------------------------------------------------------------------------------
@ Copy memory if length	!= 0
@  r1 = Source Address
@  r2 = Dest Address
@  r4 = Dest Address + Length
@---------------------------------------------------------------------------------
CopyMemCheck:
@---------------------------------------------------------------------------------
	sub	r3, r4, r2		@ Is there any data to copy?
@---------------------------------------------------------------------------------
@ Copy memory
@  r1 = Source Address
@  r2 = Dest Address
@  r3 = Length
@---------------------------------------------------------------------------------
CopyMem:
@---------------------------------------------------------------------------------
	mov	r0, #3			@ These commands are used in cases where
	add	r3, r3, r0		@ the length is not a multiple of 4,
	bics	r3, r3, r0		@ even though it should be.
	bxeq	lr			@ Length is zero, so exit
CIDLoop:
	ldmia	r1!, {r0}
	stmia	r2!, {r0}
	subs	r3, r3, #4
	bne	CIDLoop
	bx	lr

.global fastCopy32
.type	fastCopy32 STT_FUNC
@ r0 : src, r1 : dst, r2 : len
fastCopy32:
	stmfd   sp!, {r3-r11,lr}
	@ copy 512 bytes
	mov     r10, r0
	mov     r9, r1
	mov     r8, r2
loop_fastCopy32:
	ldmia   r10!, {r0-r7}
	stmia   r9!,  {r0-r7}
	subs    r8, r8, #32  @ 4*8 bytes
	bgt     loop_fastCopy32
	ldmfd   sp!, {r3-r11,lr}
	bx      lr

@---------------------------------------------------------------------------------
	.align
	.pool
	.end
@---------------------------------------------------------------------------------