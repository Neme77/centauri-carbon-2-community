/* Port of the tested HTTP LAN/WAN v1 token fallback.
 * Preserve original strcmp match. Only WAN + protected LAN token permits
 * the additional saved LAN credential. Original empty-request guards stay.
 * DeviceStatus flags: d90=LAN mode, d91=token enabled; d94=LAN string,
 * d98=string length. Entry: r0=request chars, r1=active expected chars.
 */
push {r4, r5, r6, lr}
mov r4, r0
bl 0xc0090
cmp r0, #0
beq done
mov r6, r0
bl 0x87ab00
mov r5, r0
ldrb r1, [r5, #0xd90]
cmp r1, #0
bne reject
ldrb r1, [r5, #0xd91]
cmp r1, #0
beq reject
ldr r1, [r5, #0xd98]
cmp r1, #0
beq reject
add r0, r5, #0xd90
add r0, r0, #4
bl 0xbf7f0
mov r1, r0
mov r0, r4
bl 0xc0090
b done
reject:
mov r0, r6
done:
pop {r4, r5, r6, pc}
