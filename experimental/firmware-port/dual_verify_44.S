.syntax unified
.arm
.section .text
.global dual_verify_wrapper
.equ VERIFY_STOCK, 0x3c16c
.equ BIO_FREE, 0x15828
.equ BIO_NEW_MEM_BUF, 0x15894
.equ EVP_PKEY_CTX_NEW, 0x159f0
.equ PEM_READ_BIO_PUBKEY, 0x15aec
.equ EVP_PKEY_VERIFY, 0x15af8
.equ EVP_PKEY_VERIFY_INIT, 0x15ef4
.equ EVP_PKEY_CTX_CTRL, 0x16068
.equ EVP_SHA256, 0x16110
.equ EVP_PKEY_CTX_FREE, 0x164b8
.equ MUTEX_LOCK, 0x157e0
.equ MUTEX_UNLOCK, 0x16578
// Reserved zero-fill memory in the extended writable PT_LOAD (new OTA).
// glibc 2.23 ARM normal static mutex: zero initializer, reserve 64 bytes.
.equ CACHE_MUTEX, 0xa8ec0
.equ CACHE_KEY, 0xa8f00

dual_verify_wrapper:
    push {r4, lr}
    mov r4, r0
    bl VERIFY_STOCK
    cmp r0, #0
    beq wrapper_done
    mov r0, r4
    bl community_verify
wrapper_done:
    pop {r4, pc}

community_verify:
    push {r4-r8, lr}
    sub sp, sp, #8
    mov r8, r0
    mov r6, #0
    mvn r7, #0
    movw r0, #0x8ec0
    movt r0, #0xa
    bl MUTEX_LOCK
    cmp r0, #0
    bne return_unlocked
    movw r4, #0x8f00
    movt r4, #0xa
    ldr r5, [r4]
    cmp r5, #0
    bne have_key
    adr r0, community_pem
    movw r1, #451
    bl BIO_NEW_MEM_BUF
    mov r4, r0
    cmp r4, #0
    beq cleanup
    mov r0, r4
    mov r1, #0
    mov r2, #0
    mov r3, #0
    bl PEM_READ_BIO_PUBKEY
    mov r5, r0
    mov r0, r4
    bl BIO_FREE
    cmp r5, #0
    beq cleanup
    movw r4, #0x8f00
    movt r4, #0xa
    str r5, [r4]
have_key:
    mov r0, r5
    mov r1, #0
    bl EVP_PKEY_CTX_NEW
    mov r6, r0
    cmp r6, #0
    beq cleanup
    mov r0, r6
    bl EVP_PKEY_VERIFY_INIT
    cmp r0, #0
    ble cleanup
    bl EVP_SHA256
    str r0, [sp, #4]
    mov r0, #0
    str r0, [sp]
    mov r3, #1
    mov r2, #0xf8
    mvn r1, #0
    mov r0, r6
    bl EVP_PKEY_CTX_CTRL
    cmp r0, #0
    ble cleanup
    add r1, r8, #0x100
    add r3, r8, #0xe0
    mov r2, #256
    mov r0, #32
    str r0, [sp]
    mov r0, r6
    bl EVP_PKEY_VERIFY
    cmp r0, #1
    moveq r7, #0
    mvnne r7, #0
cleanup:
    cmp r6, #0
    beq unlock
    mov r0, r6
    bl EVP_PKEY_CTX_FREE
unlock:
    movw r0, #0x8ec0
    movt r0, #0xa
    bl MUTEX_UNLOCK
    cmp r0, #0
    mvnne r7, #0
return_unlocked:
    mov r0, r7
    add sp, sp, #8
    pop {r4-r8, pc}
.align 2
community_pem:
    .incbin "cc2_community_release_public.pem"
.align 2
