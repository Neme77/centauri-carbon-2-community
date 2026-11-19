.syntax unified
.arm
.text
.global prefeed_fixture
.type prefeed_fixture, %function
prefeed_fixture:
    push {fp, lr}
    mov fp, sp
    sub sp, sp, #96
    str r0, [fp, #-88]
    str r1, [fp, #-64]
    b canvas_live_loading_branch
.size prefeed_fixture, .-prefeed_fixture

@ The test linker places this section at the stock runout branch. The other
@ two entries occupy their stock addresses and return through the fake frame.
.section .native_calls,"ax",%progbits
    mov r0, #1
    b .Lreturn
.org 0x164
    bl receive_motor
    b .Lreturn
.org 0x280
    mov r0, #0
.Lreturn:
    mov sp, fp
    pop {fp, pc}
.section .note.GNU-stack,"",%progbits
