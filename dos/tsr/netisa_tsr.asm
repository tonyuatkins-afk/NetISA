; ============================================================================
; NETISA.COM - NetISA INT 63h TSR
;
; Terminate-and-Stay-Resident program that hooks INT 63h to provide
; the NetISA software API. Skeleton implementation with stub handlers.
;
; Build:  nasm -f bin -o netisa.com netisa_tsr.asm
; Usage:  NETISA [/A:hhh] [/I:nn] [/POLL]
;
; Resident size target: under 2KB
;
; See docs/netisa-architecture-spec.md Section 4 for the full API.
; ============================================================================

[BITS 16]
[ORG 0x100]

; ============================================================================
; CONSTANTS
; ============================================================================

INT_VECTOR      equ 0x63
SIGNATURE       equ 0x4352      ; 'CR' for presence check
SIG_LEN         equ 8

FW_MAJOR        equ 1
FW_MINOR        equ 0
FW_PATCH        equ 1

; ============================================================================
; TSR ENTRY POINT
; ============================================================================

start:
    jmp     install             ; Jump to non-resident installation code

; ============================================================================
; RESIDENT DATA (stays in memory after TSR)
; ============================================================================

resident_start:

SIG_STRING      db  'NETISA10'  ; 8-byte signature for scanning (after jmp)
old_int63       dd  0           ; Saved original INT 63h vector
base_addr       dw  0x280       ; Card base I/O address (configurable)

; ============================================================================
; INT 63h HANDLER (resident)
;
; Calling convention:
;   AH = function group
;   AL = function number
;   Other registers as specified per function
;   Returns: CF clear on success, CF set on error, AX = error code
; ============================================================================

int63_handler:
    sti                         ; Re-enable interrupts (soft INT clears IF)
    push    bp
    mov     bp, sp

    ; Dispatch on AH (function group)
    cmp     ah, 0x00
    je      .group_system
    cmp     ah, 0x01
    je      .group_netcfg
    cmp     ah, 0x02
    je      .group_dns
    cmp     ah, 0x03
    je      .group_session
    cmp     ah, 0x05
    je      .group_crypto
    cmp     ah, 0x07
    je      .group_diag

    ; Unknown group: return ERR_NOT_IMPLEMENTED
    mov     ax, 0x0014
    stc
    pop     bp
    iret

; ------ Group 0x00: System ------
.group_system:
    cmp     al, 0x00            ; NOP / Presence check
    je      .sys_nop
    cmp     al, 0x01            ; Get card status
    je      .sys_status
    cmp     al, 0x02            ; Reset card
    je      .sys_reset
    cmp     al, 0x03            ; Get network status
    je      .sys_netstatus
    cmp     al, 0x05            ; Get firmware version
    je      .sys_fwversion

    ; Unhandled function
    mov     ax, 0x0014
    stc
    pop     bp
    iret

.sys_nop:
    ; Return signature in AX, version in BH/BL/CH
    mov     ax, SIGNATURE
    mov     bh, FW_MAJOR
    mov     bl, FW_MINOR
    mov     ch, FW_PATCH
    clc
    pop     bp
    iret

.sys_status:
    ; Stub: return CMD_READY + BOOT_COMPLETE flags
    mov     al, 0x41            ; status flags
    mov     ah, 0               ; active sessions
    mov     bl, 4               ; max sessions
    clc
    pop     bp
    iret

.sys_reset:
    ; Stub: just return success
    clc
    pop     bp
    iret

.sys_netstatus:
    ; Stub: return disconnected
    mov     al, 0               ; disconnected
    mov     ah, 0               ; signal 0%
    clc
    pop     bp
    iret

.sys_fwversion:
    mov     bh, FW_MAJOR
    mov     bl, FW_MINOR
    mov     ch, FW_PATCH
    clc
    pop     bp
    iret

; ------ Group 0x01: Network Config ------
.group_netcfg:
    cmp     al, 0x04
    je      .net_scan

    ; Most netcfg functions: stub success
    clc
    pop     bp
    iret

.net_scan:
    ; Stub: return 0 networks
    xor     ax, ax
    clc
    pop     bp
    iret

; ------ Group 0x02: DNS ------
.group_dns:
    mov     ax, 0x0014          ; Not implemented
    stc
    pop     bp
    iret

; ------ Group 0x03: Sessions ------
.group_session:
    mov     ax, 0x0014          ; Not implemented
    stc
    pop     bp
    iret

; ------ Group 0x05: Crypto ------
.group_crypto:
    cmp     al, 0x0B            ; Random bytes
    je      .crypto_random

    mov     ax, 0x0014
    stc
    pop     bp
    iret

.crypto_random:
    ; Stub: fill with pseudo-random from BIOS timer
    push    cx
    push    di
    push    es
    ; ES:DI already points to buffer, CX = count
.rand_loop:
    cmp     cx, 0
    je      .rand_done
    push    cx
    xor     ax, ax
    int     0x1A                ; Read timer tick into DX
    pop     cx
    mov     al, dl
    stosb
    dec     cx
    jmp     .rand_loop
.rand_done:
    pop     es
    pop     di
    pop     cx
    clc
    pop     bp
    iret

; ------ Group 0x07: Diagnostics ------
.group_diag:
    cmp     al, 0x00
    je      .diag_uptime

    mov     ax, 0x0014
    stc
    pop     bp
    iret

.diag_uptime:
    ; Return BIOS ticks / 18 as seconds in DX:AX (32-bit result)
    ; INT 1Ah returns CX:DX = 32-bit tick count (~18.2 ticks/sec)
    ; Two-stage 16-bit divide avoids overflow on 8088:
    ;   Step 1: 0:high_word / 18 -> high quotient + remainder
    ;   Step 2: remainder:low_word / 18 -> low quotient
    push    bx
    push    cx
    xor     ax, ax
    int     0x1A                ; CX:DX = tick count (CX=high, DX=low)
    push    dx                  ; save low word of ticks
    mov     ax, cx              ; AX = high word of ticks
    xor     dx, dx              ; DX:AX = 0:high_ticks
    mov     bx, 18
    div     bx                  ; AX = high quotient, DX = remainder
    mov     cx, ax              ; CX = high quotient (saved)
    pop     ax                  ; AX = low word of ticks
    ; DX = remainder from high divide (still set from div above)
    div     bx                  ; DX:AX / 18 -> AX = low quotient
    mov     dx, cx              ; DX = high quotient
    ; Result: DX:AX = uptime in seconds
    pop     cx
    pop     bx
    clc
    pop     bp
    iret

resident_end:

; ============================================================================
; NON-RESIDENT CODE (freed after going TSR)
; ============================================================================

install:
    ; Print banner
    mov     dx, msg_banner
    mov     ah, 0x09
    int     0x21

    ; Check if already loaded by scanning INT 63h for our signature
    push    es
    xor     ax, ax
    mov     es, ax
    mov     bx, [es:INT_VECTOR*4]       ; Offset of current handler
    mov     ax, [es:INT_VECTOR*4+2]     ; Segment of current handler
    pop     es

    ; Check if vector is 0000:0000 (uninitialized)
    or      bx, ax
    jz      .not_loaded

    ; Try calling INT 63h with presence check
    mov     ah, 0x00
    mov     al, 0x00
    int     INT_VECTOR
    jc      .not_loaded         ; CF set = not our handler
    cmp     ax, SIGNATURE
    jne     .not_loaded

    ; Already loaded
    mov     dx, msg_already
    mov     ah, 0x09
    int     0x21
    mov     ax, 0x4C00
    int     0x21

.not_loaded:
    ; Parse command line for /A:hhh
    call    parse_cmdline

    ; Install INT 63h handler
    ; Save old vector
    push    es
    xor     ax, ax
    mov     es, ax
    mov     ax, [es:INT_VECTOR*4]
    mov     word [old_int63], ax
    mov     ax, [es:INT_VECTOR*4+2]
    mov     word [old_int63+2], ax

    ; Set new vector to our handler
    cli
    mov     word [es:INT_VECTOR*4], int63_handler
    mov     [es:INT_VECTOR*4+2], cs
    sti
    pop     es

    ; Print installed message
    mov     dx, msg_installed
    mov     ah, 0x09
    int     0x21

    ; Print base address
    mov     dx, msg_base
    mov     ah, 0x09
    int     0x21
    mov     ax, [base_addr]
    call    inst_print_hex16
    mov     dx, msg_crlf
    mov     ah, 0x09
    int     0x21

    ; Go TSR: keep everything from PSP to resident_end
    ; DX = paragraphs to keep = (resident_end - PSP_start + 15) / 16
    mov     dx, resident_end
    sub     dx, start           ; Offset from ORG
    add     dx, 0x100           ; Add PSP size (256 bytes)
    add     dx, 15
    shr     dx, 1               ; Convert to paragraphs (4x shr for 8088)
    shr     dx, 1
    shr     dx, 1
    shr     dx, 1
    mov     ax, 0x3100          ; TSR, exit code 0
    int     0x21

; ------ Command line parser ------
parse_cmdline:
    push    ax
    push    bx
    push    cx
    push    si

    mov     si, 0x81
    mov     cl, [0x80]
    xor     ch, ch

.cmd_scan:
    cmp     cx, 0
    je      .cmd_done
    lodsb
    dec     cx
    cmp     al, '/'
    jne     .cmd_scan
    cmp     cx, 0
    je      .cmd_done
    lodsb
    dec     cx
    or      al, 0x20
    cmp     al, 'a'
    jne     .cmd_scan
    cmp     cx, 0
    je      .cmd_done
    lodsb
    dec     cx
    cmp     al, ':'
    jne     .cmd_scan

    ; Parse hex digits
    xor     bx, bx
.cmd_hex:
    cmp     cx, 0
    je      .cmd_hex_done
    lodsb
    dec     cx
    cmp     al, '0'
    jb      .cmd_hex_done
    cmp     al, '9'
    jbe     .cmd_09
    or      al, 0x20
    cmp     al, 'a'
    jb      .cmd_hex_done
    cmp     al, 'f'
    ja      .cmd_hex_done
    sub     al, 'a' - 10
    jmp     .cmd_add
.cmd_09:
    sub     al, '0'
.cmd_add:
    shl     bx, 1               ; 4x shl for 8088 compatibility
    shl     bx, 1
    shl     bx, 1
    shl     bx, 1
    xor     ah, ah
    add     bx, ax
    jmp     .cmd_hex
.cmd_hex_done:
    cmp     bx, 0
    je      .cmd_done
    mov     [base_addr], bx

.cmd_done:
    pop     si
    pop     cx
    pop     bx
    pop     ax
    ret

; Print AX as 4-digit hex
inst_print_hex16:
    push    ax
    push    cx
    push    dx

    mov     cx, 4
    ; Start with high nibble
.hex_loop:
    rol     ax, 4
    push    ax
    and     al, 0x0F
    add     al, '0'
    cmp     al, '9'
    jbe     .hex_ok
    add     al, 7
.hex_ok:
    mov     dl, al
    mov     ah, 0x02
    int     0x21
    pop     ax
    loop    .hex_loop

    pop     dx
    pop     cx
    pop     ax
    ret

; ============================================================================
; NON-RESIDENT DATA (freed after going TSR)
; ============================================================================

msg_banner:     db  0x0D, 0x0A
                db  'NetISA TSR v1.0.1 - INT 63h API Handler', 0x0D, 0x0A
                db  'Copyright (c) 2026 NetISA Project', 0x0D, 0x0A, '$'

msg_already:    db  'NetISA TSR already resident.', 0x0D, 0x0A, '$'

msg_installed:  db  'Installed. INT 63h handler active.', 0x0D, 0x0A, '$'

msg_base:       db  'Base I/O address: 0x$'

msg_crlf:       db  0x0D, 0x0A, '$'
