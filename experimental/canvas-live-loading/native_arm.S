.syntax unified
.arm
.text
.align 2
.global canvas_live_loading_branch
.hidden canvas_live_loading_branch
.hidden canvas_live_loading_plan
.type canvas_live_loading_branch, %function
canvas_live_loading_branch:
    ldr r0, [fp, #-88]         @ Canvas *this
    ldr r1, [fp, #-64]         @ inlet that just reported insertion
    sub r2, fp, #80            @ existing FeederMotor storage in the native frame
    bl canvas_live_loading_plan
    cmp r0, #1                @ PREFEED_RUNOUT
    ldreq pc, .Lrunout
    cmp r0, #2                @ PREFEED_GRIP
    ldrne pc, .Lfinish
    ldr r0, [fp, #-88]
    ldr r0, [r0]              @ CanvasProtocol held by the first shared_ptr
    ldr r1, [fp, #-64]
    sub r2, fp, #80
    mov r3, #0                @ native asynchronous feeder request
    ldr pc, .Lsend
.Lrunout:
    .word 0x005e0254          @ stock runout deadline guard
.Lfinish:
    .word 0x005e04d4          @ native per-slot timers and loop continuation
.Lsend:
    .word 0x005e03b8          @ stock feeder call site, with its original unwind frame
.size canvas_live_loading_branch, .-canvas_live_loading_branch
.section .note.GNU-stack,"",%progbits
