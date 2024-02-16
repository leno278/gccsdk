@ Copyright (c) 2019-2021 GCCSDK Developers
@ Written by Lee Noar

#include "decl-asm.h"
#include "debug-asm.h"

.set	STORE_OP,	0
.set	LOAD_OP,	1
.set	MAP_OP,		2

	.text

abort_handler:
	STR	r0, registers_0
	SUB	r0, r14, #8
	STR	r0, registers_PC

	MRS	r0, CPSR_all
	STR	r0, registers_CPSR		@ Store the handler CPU state

	MRS	r0, SPSR_all			@ Read aborting mode CPU state
	STR	r0, registers_SPSR		@ Store the aborting SPSR
	TST	r0, #0xF			@ is it USR?
	BEQ	USR_mode
	MSR	CPSR_all, r0			@ If not USR, switch to aborting CPU state
	ADR	r0, registers_0 + 4
	STMIA	r0, {r1-r14}			@ store the aborting state registers
	B	1f
USR_mode:
	ADR	r0, registers_0 + 4
	STMIA	r0!, {r1-r12}
	STMIA	r0, {sp, lr}^
	MRS	r6, SPSR_all
	STR	r6, registers_SPSR		@ store the aborting SPSR
1:
	ADR	r7, data_abort_support_block

	MRS	r9, CPSR
	BIC	r0, r9, #0x1F			@ Clear all mode bits
	ORR	r0, r0, #0x13			@ Set SVC mode bits
	MSR	CPSR_c, r0			@ Switch to SVC
	STR	lr, [sp, #-4]!			@ Preserve R14_svc
	@ No SWIs before this point, otherwise we corrupt SVC_lr

	@ Guard against an abort in this abort handler, as soon as possible at least
	LDR	r0, [r7, #ABORT_BLOCK_IN_HANDLER]
	TEQ	r0, #1
	BEQ	handler__internal_abort

	@ Read the address of the fault
	MRC	P15, 0, r8, C6, C0

	MOV	r0, #1
	STR	r0, [r7, #ABORT_BLOCK_IN_HANDLER]

	LDR	r10, [r7, #ABORT_BLOCK_FIRST_HANDLER]
	TEQ	r10, #0
	BEQ	handler__pass_on
	ADR	r11, registers_0
	MOV	r1, r8					@ address of fault
0:
	LDR	r12, [r10, #ABORT_HANDLER_R12_DATA]
	MOV	lr, pc
	LDR	pc, [r10, #ABORT_HANDLER_ROUTINE]
	AND	r2, r0, #0xFF
	TEQ	r2, #ABORT_HANDLER_CLAIM
	BEQ	handler__exit
	TEQ	r2, #ABORT_HANDLER_ERROR
	BEQ	handler__error

	LDR	r10, [r10, #LINKHDR_NEXT]
	TEQ	r10, #0
	BNE	0b

	.if DEBUG_HANDLER_DUMP_REGS == 1
	B	handler__unhandled_abort
	.endif

handler__pass_on:
	LDR	lr, [sp], #4			@ Restore R14_svc

	LDR	r0, [r7, #ABORT_BLOCK_OLD_HANDLER]
	STR	r0, registers_PC

	LDR	r9, registers_CPSR

	MOV	r0, #0
	STR	r0, [r7, #ABORT_BLOCK_IN_HANDLER]

	ADR	r0, registers_0
	MSR	SPSR_cxsf, r9
	LDMIA	r0, {r0-pc}^			@ Restore all registers and return to old handler

	@ r0 = return value from handler routine
handler__error:
	MOV	r0, r0, LSR#8
	AND	r0, r0, #0xFF
	STR	r0, [r7, #ABORT_BLOCK_STATUS]
	B	handler__pass_on

handler__internal_abort:
	MOV	r0, #ABORT_ERROR_IN_ABORT_HANDLER
	STR	r0, [r7, #ABORT_BLOCK_STATUS]
	B	handler__pass_on

	.if DEBUG_HANDLER_DUMP_REGS == 1
handler__unhandled_abort:
	REPORT_TEXT "Unhandled abort:"
	REPORT_HEX r6

handler__dump_regs:
	MRC	P15, 0, r12, C6, C0
	REPORT_TEXT "Fault address:"
	REPORT_HEX r12
	REPORT_TEXT "Fault instruction at:"
	LDR	r0, registers_PC
	REPORT_HEX r0
	MOV 	r0, #0
	ADR	r1, registers_0
2:
	LDR	r2, [r1, r0, LSL#2]
	REPORT_DEC r0
	REPORT_HEX r2
	ADD	r0, r0, #1
	CMP	r0, #18
	BLT	2b
	B	handler__pass_on
	.endif

handler__exit:
	LDR	lr, [sp], #4			@ Restore R14_svc

	LDR	r9, registers_SPSR

	MOV	r0, #0
	STR	r0, [r7, #ABORT_BLOCK_IN_HANDLER]

	ADR	lr, registers_0
	MSR	SPSR_cxsf, r9

	@ Restore all registers and return to aborting instruction
	TST	r9, #0xF
	LDMNEIA	lr, {r0-pc}^			@ Non USR mode return
	LDMIA	lr, {r0-lr}^			@ USR mode return
	LDR	lr, [lr, #15*4]
	MOVS	pc, lr

	.global	abort_install
abort_install:
	STMFD	sp!, {r1-r3, r12, lr}

	ADR	r12, data_abort_support_block

	MOV	r0, #3
	ADRL	r1, abort_handler
	MOV	r2, #0
	MOV	r3, #0
	SWI	XOS_ChangeEnvironment
	STRVC	r1, [r12, #ABORT_BLOCK_OLD_HANDLER]
	MOVVC	r0, #0

	LDMFD	sp!, {r1-r3, r12, pc}

	.global	abort_remove
abort_remove:
	STMFD	sp!, {r1-r3, r12, lr}

	ADR	r12, data_abort_support_block

	LDR	r1, [r12, #ABORT_BLOCK_OLD_HANDLER]

	TEQ	r1, #0
	LDMEQFD	sp!, {r1-r3, r12, pc}

	MOV	r0, #0
	STR	r0, [r12, #ABORT_BLOCK_OLD_HANDLER]

	MOV	r0, #SYSTEMCONTROLHANDLER_DATAABORT
	MOV	r2, #0
	MOV	r3, #0
	SWI	XOS_ChangeEnvironment
	MOVVC	r0, #0

	LDMFD	sp!, {r1-r3, r12, pc}

	.global	get_data_abort_support_block
get_data_abort_support_block:
	ADR	r0, data_abort_support_block
	MOV	pc, lr

	.global abort_read_error_status
abort_read_error_status:
	ADR	r0, data_abort_support_block
	LDR	r0, [r0, #ABORT_BLOCK_STATUS]
	MOV	pc, lr

data_abort_support_block:
	.space	ABORT_BLOCK_SIZE

registers_0:
	.space	15*4
registers_PC:
	.space	4
registers_SPSR:
	@ CPU mode of the aborting instruction
	.space	4
registers_CPSR:
	@ CPU mode on entry to abort handler
	.space	4

@ Entry:
@  r12 = ptr to module globals
pre_wimp_filter:
	STMFD	sp!, {r0, r8, r11, lr}
	MRS	r8, CPSR

	ADR	r11, data_abort_support_block
	MOV	r0, #0
	STR	r0, [r11, #ABORT_BLOCK_FIRST_HANDLER]

	MSR	CPSR_f, r8
	LDMFD	sp!, {r0, r8, r11, pc}

@ Exit:
@  r0 = ptr to pre wimp_filter routine
	.global	abort_get_pre_filter
abort_get_pre_filter:
	ADR	r0, pre_wimp_filter
	MOV	pc, lr

@ Exit:
@  r0 = ptr to post wimp_filter routine
	.global	abort_get_post_filter
abort_get_post_filter:
	ADR	r0, post_wimp_filter
	MOV	pc, lr

@ Entry:
@  r12 = ptr to module globals
post_wimp_filter:
	STMFD	sp!, {r0-r2, r8, r9, r11, lr}
	MRS	r8, CPSR
	MOV	r9, r12

	@ Convert 0x8000 logical address to its physical address
	SUB	sp, sp, #12
	MOV	r0, #0
	STR	r0, [sp, #0]
	STR	r0, [sp, #8]
	MOV	r0, #0x8000
	STR	r0, [sp, #4]
	MOV	r0, #(1 << 9) | (1 << 13)
	MOV	r1, sp
	MOV	r2, #1
	SWI	XOS_Memory
	LDRVC	r2, [sp, #8]
	ADD	sp, sp, #12
	BVS	postfilter__exit

	LDR	r0, [r9, #GLOBALS_APP_LIST + LINKLIST_FIRST]
	TEQ	r0, #0
	BEQ	postfilter__exit
0:
	LDR	r1, [r0, #APP_ID]
	TEQ	r1, r2
	BEQ	postfilter__app_found
	LDR	r0, [r0, #APP_LINK + LINKHDR_NEXT]
	TEQ	r0, #0
	BNE	0b
	B	postfilter__exit
postfilter__app_found:
	ADR	r11, data_abort_support_block
	LDR	r1, [r0, #APP_ABORT_HANDLER_LIST + LINKLIST_FIRST]
	STR	r1, [r11, #ABORT_BLOCK_FIRST_HANDLER]
	@ Fall through
postfilter__exit:
	MSR	CPSR_f, r8
	LDMFD	sp!, {r0-r2, r8, r9, r11, pc}

@  r0 - Reason code 6
@  r1 - Requested change amount (signed page count)
@  r2 - Dynamic area number
@ r12 - Workspace pointer passed to OS_DynamicArea 0
	.global da_handler
da_handler:
	CMP	r0, #DYNAMICAREA_HANDLER_REASON_ABORT
	BEQ	aborttrap_handler

	@ Set V flag.
	CMP     r0, #1<<31
	CMNVC   r0, #1<<31

	@ Return a generic error rather than returning our own.
	MOV	r0, #0
	MOV	pc, lr

	@ Entry:
	@  R0	5 (Dynamic Area handler reason)
	@  R1	AbortTrap reason code + flags
	@    Bits 0-3: Reason code:
	@	0 = Store operation. Bit 4 indicates if the access is privileged (1) or unprivileged (0).
	@	1 = Load operation. Bit 4 indicates if the access is privileged (1) or unprivileged (0).
	@	2 = Memory map operation. Bits 4-9 indicate the required permissions.
	@	3+ Reserved
	@	Bits 4+: Flags
	@  R2	Pointer to buffer to use as data source (AbortTrap reason 0) or destination (AbortTrap reason 1). Invalid for memmap requests (AbortTrap reason 2).
	@  R3	Base address of memory being accessed
	@  R4	Length of memory being accessed (bytes)
	@  R5	Current end address of allocated portion of dynamic area (invalid for sparse or PMP areas)
	@  R12	Workspace pointer passed to OS_DynamicArea 0 or base of area
	@ Exit
	@  R4	Amount of data processed (= entry R4 if all processed)
	@  All other registers preserved

aborttrap_handler:
	STMFD	sp!, {r1-r3, r5-r12, lr}

	REPORT_TEXT "aborttrap_handler"
@	REPORT_DEC r1
@	REPORT_HEX r2
	REPORT_HEX r3
@	REPORT_DEC r4
@	REPORT_HEX r5

	MOV	r7, r12

	@ Guard against an abort in this abort handler, as soon as possible at least
	LDR	r0, [r7, #ABORT_BLOCK_IN_HANDLER]
	TEQ	r0, #1
	BEQ	aborttrap_handler__internal_abort

	MOV	r0, #1
	STR	r0, [r7, #ABORT_BLOCK_IN_HANDLER]

	LDR	r10, [r7, #ABORT_BLOCK_FIRST_HANDLER]
	TEQ	r10, #0
	BEQ	aborttrap_handler__unhandled

	MOV	r9, r1				@ r9 = reason code, 0 = store, 1 = load
	MOV	r1, r3				@ address of fault
0:
	LDR	r12, [r10, #ABORT_HANDLER_R12_DATA]

	MOV	lr, pc
	LDR	pc, [r10, #ABORT_HANDLER_ROUTINE]

	AND	r8, r0, #0xFF
	TEQ	r8, #ABORT_HANDLER_CLAIM
	BEQ	aborttrap_handler__exit
	TEQ	r8, #ABORT_HANDLER_ERROR
	BEQ	aborttrap_handler__error

	LDR	r10, [r10, #LINKHDR_NEXT]
	TEQ	r10, #0
	BNE	0b
	@ Fall through, no handler was able to handle the abort

aborttrap_handler__unhandled:
	MOV	r0, #ABORT_ERROR_UNHANDLED
	STR	r0, [r7, #ABORT_BLOCK_STATUS]

	@ Set V flag.
	CMP     r0, #1<<31
	CMNVC   r0, #1<<31

	MOV	r4, #0

	MOV	r0, #0
	STR	r0, [r7, #ABORT_BLOCK_IN_HANDLER]

	@ Return a generic error rather than returning our own (ie, r0 = 0)
	LDMFD	sp!, {r1-r3, r5-r12, pc}

aborttrap_handler__internal_abort:
	MOV	r0, #ABORT_ERROR_IN_ABORT_HANDLER
	STR	r0, [r7, #ABORT_BLOCK_STATUS]

aborttrap_handler__error:
	@ Handler error exit:
	@   V flag set
	@   r0 = 0 for generic error
	@   r4 = 0
	@   Store the returned error in the abort block
	MOV	r0, r0, LSR#8
	AND	r0, r0, #0xFF
	STR	r0, [r7, #ABORT_BLOCK_STATUS]

	@ Set V flag.
	CMP     r0, #1<<31
	CMNVC   r0, #1<<31

	@ Return a generic error rather than returning our own.
	MOV	r0, #0
	STR	r0, [r7, #ABORT_BLOCK_IN_HANDLER]

	LDMFD	sp!, {r1-r3, r5-r12, pc}

aborttrap_handler__exit:
	@ Normal exit:
	@   V flag clear
	@   Return 0 in r0
	@   Return r4 as it was on entry
	REPORT_TEXT "data size:"
	REPORT_DEC r4

	TEQ	r9, #STORE_OP
	BNE	aborttrap_handler__loadop

aborttrap_handler__storeop:
	TEQ	r4, #1
	LDREQB	r0, [r2, #0]
	STREQB	r0, [r3, #0]
	BEQ	1f
	TEQ	r4, #2
	BNE	0f
	LDRB	r0, [r2, #0]
	STRB	r0, [r3, #0]
	LDRB	r0, [r2, #1]
	STRB	r0, [r3, #1]
	B	1f
0:
	RSB	r1, r4, #(15 * 4)
	ADD	r1, pc, r1, LSL#1
	@ Following instruction is required to align start of jump table
	@ which is why it isn't folded into the one above.
	MOV	pc, r1

	@ Enough load/stores to emulate a full STMFD with all registers used.
	LDR	r0, [r2], #4
	STR	r0, [r3], #4
	LDR	r0, [r2], #4
	STR	r0, [r3], #4
	LDR	r0, [r2], #4
	STR	r0, [r3], #4
	LDR	r0, [r2], #4
	STR	r0, [r3], #4
	LDR	r0, [r2], #4
	STR	r0, [r3], #4
	LDR	r0, [r2], #4
	STR	r0, [r3], #4
	LDR	r0, [r2], #4
	STR	r0, [r3], #4
	LDR	r0, [r2], #4
	STR	r0, [r3], #4
	LDR	r0, [r2], #4
	STR	r0, [r3], #4
	LDR	r0, [r2], #4
	STR	r0, [r3], #4
	LDR	r0, [r2], #4
	STR	r0, [r3], #4
	LDR	r0, [r2], #4
	STR	r0, [r3], #4
	LDR	r0, [r2], #4
	STR	r0, [r3], #4
	LDR	r0, [r2], #4
	STR	r0, [r3], #4
	LDR	r0, [r2], #4
	STR	r0, [r3], #4
1:
	@ Return value of 0 in r0 and clear V flag
	MOVS	r0, #ABORT_ERROR_NONE

	@ Clear error indicator
	STR	r0, [r7, #ABORT_BLOCK_STATUS]

	@ Clear in handler flag
	STR	r0, [r7, #ABORT_BLOCK_IN_HANDLER]

	@ r4 preserved from entry
	LDMFD	sp!, {r1-r3, r5-r12, pc}

aborttrap_handler__loadop:
REPORT_TEXT "load_op"
	TEQ	r9, #MAP_OP
	MOVEQ	r0, #(ABORT_ERROR_UNHANDLED << 8)
	STREQ	r0, [r7, #ABORT_BLOCK_STATUS]
	BEQ	aborttrap_handler__error

	MOV	r0, #0
	TEQ	r4, #1
	STREQB	r0, [r2, #0]
	BEQ	0f
	TEQ	r4, #2
	STREQB	r0, [r2, #0]
	STREQB	r0, [r2, #1]
	BEQ	0f

	RSB	r1, r4, #(15 * 4)
	ADD	r1, pc, r1
	@ Following instruction is required to align start of jump table
	@ which is why it isn't folded into the one above.
@	MOV	pc, r1
  B 1f
	STR	r0, [r2], #4
	STR	r0, [r2], #4
	STR	r0, [r2], #4
	STR	r0, [r2], #4
	STR	r0, [r2], #4
	STR	r0, [r2], #4
	STR	r0, [r2], #4
	STR	r0, [r2], #4
	STR	r0, [r2], #4
	STR	r0, [r2], #4
	STR	r0, [r2], #4
	STR	r0, [r2], #4
	STR	r0, [r2], #4
	STR	r0, [r2], #4
	STR	r0, [r2], #4
1:
  REPORT_TEXT "Jump to:"
  REPORT_HEX r1
  MOV r0,#(ABORT_ERROR_UNHANDLED << 8)
  B	aborttrap_handler__error
0:
	@ Return value of 0 in r0 and clear V flag
	MOVS	r0, #ABORT_ERROR_NONE

	@ Clear error indicator
	STR	r0, [r7, #ABORT_BLOCK_STATUS]

	@ Clear in handler flag
	STR	r0, [r7, #ABORT_BLOCK_IN_HANDLER]

	@ r4 preserved from entry
	LDMFD	sp!, {r1-r3, r5-r12, pc}

	.end
