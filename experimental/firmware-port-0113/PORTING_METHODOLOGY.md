# Porting binary patches to a recompiled vendor base

A general, reusable method for moving an existing byte-level patch from one
compiled build of a vendor binary to a newer build of the same program, as when
ELEGOO ships a new base (e.g. 02.01.01.07 → 02.01.01.13) that recompiles the
same source with shifted code.

This document describes the **technique**. It does not list patch locations;
each concrete patch site is identified and verified by the maintainer, per
`AGENTS.md` and `docs/TESTING.md`, and nothing is applied without hardware
testing.

## Core principle: offsets are per-function, never global

A recompile moves code, but not by one global amount. Earlier functions grow or
shrink, so every function shifts by its own delta. Measured examples from the
02.01.01.07 → 02.01.01.13 GUI showed deltas ranging from `+0x40` to `+0x1598`
to `-0xB08` across different functions in the same binary. **Do not apply a
single global offset.** Work one function at a time.

## Prerequisites

- The CC2 vendor binaries are **not stripped**: they carry a full `.symtab`
  with function names. This is what makes name-based matching possible.
- Both builds open in Ghidra (headless import, see the analysis notes). Keep the
  **old reference build** (the exact binary the patch was authored against) and
  the **new build** side by side.

## Step-by-step

### 1. Identify the function by name, not by address
In the old build, find which function contains the old patch address (Ghidra:
the function header, or Symbol Table). Note its name and start address.
Then find the **same-named** function in the new build. Absolute addresses do
not carry over between builds; names do.

> Pitfall: the same numeric VA in the new build usually lands in a *different*
> function. Matching by raw address is the most common way to patch the wrong
> place.

### 2. Compute this function's offset
```
offset = new_function_start − old_function_start
```
This delta is valid **only inside this function**.

### 3. Derive the candidate addresses
```
new_patch_point   = old_patch_point   + offset
new_branch_target = old_branch_target + offset
```
Because the patch point and its in-function target shift by the same per-function
offset, a branch that stays *within* the function keeps the same relative
distance — so its encoded immediate is usually unchanged. A branch whose target
is in a **different** function (which shifted by a different offset) must be
recomputed with that target function's new address.

### 4. Verify in the listing before trusting the math
At `new_patch_point`, confirm the instruction is the **semantic equivalent** of
the old one:
- same kind of instruction,
- same called function name (for a `BL`),
- same surrounding structure (same switch-case, same loop).

If the instruction or context differs, the offset or the function match is
wrong. Stop and re-check step 1–2. The decompiler (identical control-flow
structure in both builds) is the strongest confirmation.

### 5. Compute the branch bytes (ARM A32)

ARM A32 `B`/`BL` is one 32-bit word, little-endian. Target rule:
```
target = instruction_address + 8 + (sign_extend(imm24) << 2)
```
To build a branch from address `A` to verified target `T`:
```
delta  = T − A − 8          # the +8 is the ARM pipeline offset
assert delta % 4 == 0        # must be word-aligned
imm24  = (delta >> 2) & 0xFFFFFF
word   = 0xEA000000 | imm24  # 0xEA = cond "always" (1110) + B opcode (1010)
                             # use 0xEB000000 for BL
bytes  = word little-endian
```
Worked example (generic): `A = 0x10300`, `T = 0x10800`
```
delta = 0x10800 − 0x10300 − 8 = 0x4F8
0x4F8 / 4 = 0x13E  → imm24 = 0x00013E
word = 0xEA00013E  → bytes: 3E 01 00 EA
```

Worked example (verified, real branch — NOT a patch). This reproduces an
**unmodified** instruction already present in the 02.01.01.13 `elegoo_printer`
build, as a check that the formula and the read addresses agree. It is a
reference instruction, not a change to apply; its bytes are exactly what the
listing already contains.

`bl communication::HTTPServer::remove_client`, read at `A = 0x857264`, target
`T = 0x859758`:
```
delta = 0x859758 − 0x857264 − 8 = 0x24EC
0x24EC mod 4 = 0                      (word-aligned)
−0x2000000 ≤ 0x24EC < 0x2000000       (in range)
imm24 = (0x24EC >> 2) & 0xFFFFFF = 0x93B
word  = 0xEB000000 | 0x93B = 0xEB00093B   (BL, base 0xEB000000)
bytes = 3B 09 00 EB
```
The listing at `0x857264` already reads `3B 09 00 EB`, so the recomputed word
matches the real instruction. This only confirms the encoding at a known-good
`bl`; it is not a ported patch and must not be injected anywhere.

### 6. Distinguish branches from data-address loads
Not every changed word is a branch. An address load uses `movw`/`movt`:
```
movw r12, #<low16>
movt r12, #<high16>
```
Its immediate changes because the referenced global moved, not because of a
jump. **Do not apply branch arithmetic to a `movw`/`movt`.** Read the new
address the pair forms and set the immediates to match it.

### 7. Record, then patch, then test
For each ported site record: function name, old/new function start, offset, old
and new patch address, old and new target, the instruction verified at the new
address, and the new bytes. Only after the whole set is verified this way does
patching happen, followed by the ARM emulation tests
(`experimental/firmware-port/test_*44_emulation.py`) and a hardware test.

## Where this fits the builder

A ported, emulation- and hardware-tested patch set is what flips
`ported_patch_set` to `true` in `builder/current/base_profiles/02.01.01.13.json`.
Until then the builder refuses to build on this base (fail-closed). The vendor
base itself (02.01.01.13) is already captured, verified and pinned; only the
patch port remains.

## Checklist per patch

- [ ] old function identified by name, start address noted
- [ ] same-named function found in new build, start address noted
- [ ] per-function offset computed
- [ ] new patch point and target derived
- [ ] instruction + context verified in the new listing
- [ ] branch vs. `movw`/`movt` distinguished
- [ ] branch bytes computed and the delta checked (÷4, in range)
- [ ] recorded in the port notes
- [ ] emulation test updated
- [ ] hardware test passed before release
