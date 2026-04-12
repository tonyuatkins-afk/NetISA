; ============================================================================
; NISATEST.COM - NetISA Phase 0 Bus Loopback Test
;
; Validates ISA bus communication with the NetISA card by writing every
; byte value (0x00-0xFF) to the data port and reading each back.
;
; Build:  nasm -f bin -o nisatest.com nisatest.asm
; Usage:  NISATEST [/A:hhh]
;         /A:hhh  - Card base I/O address in hex (default: 280)
;
; Exit codes:
;   0 = All 256 bytes passed
;   1 = Card not detected
;   2 = Loopback mismatch (data corruption)
; ============================================================================

[BITS 16]
[ORG 0x100]

section .text

start:
    ; Print banner
    mov     dx, msg_banner
    call    print_string

    ; Parse command line for /A:hhh
    call    parse_args

    ; Print base address
    mov     dx, msg_base
    call    print_string
    mov     ax, [base_addr]
    call    print_hex16
    call    print_newline

    ; ---- Step 1: Detect card ----
    mov     dx, msg_detect
    call    print_string

    ; Read status register (base+0x00)
    mov     dx, [base_addr]
    in      al, dx

    ; Reject empty bus (0xFF = no card driving the bus, ISA pull-ups)
    cmp     al, 0xFF
    je      .no_card

    ; Reject stuck-low bus (0x00 with BOOT_COMPLETE bit 6 clear).
    ; A working card with PBOOT asserted always has bit 6 set.
    ; Reading 0x00 means either a stuck-low bus fault or the card is
    ; present but not responding (CPLD unprogrammed, ESP32 not booted).
    test    al, al
    jnz     .card_ok
    ; AL is 0x00 -- bit 6 is already clear, so this is a stuck bus
    jmp     .no_card_stuck

.card_ok:
    ; Card detected. Show status byte.
    mov     dx, msg_found
    call    print_string
    call    print_hex8
    call    print_newline

    ; Check BOOT_COMPLETE flag (bit 6)
    test    al, 0x40
    jnz     .boot_ok
    mov     dx, msg_noboot
    call    print_string
    ; Continue anyway - card may be booting

.boot_ok:
    ; Read firmware version (base+0x07, 0x08, 0x09)
    mov     dx, msg_fwver
    call    print_string

    mov     dx, [base_addr]
    add     dx, 0x07
    in      al, dx          ; Major
    call    print_dec8
    mov     al, '.'
    call    print_char

    mov     dx, [base_addr]
    add     dx, 0x08
    in      al, dx          ; Minor
    call    print_dec8
    mov     al, '.'
    call    print_char

    mov     dx, [base_addr]
    add     dx, 0x09
    in      al, dx          ; Patch
    call    print_dec8
    call    print_newline

    ; ---- Step 2: Bus self-test (0x55 / 0xAA pattern) ----
    mov     dx, msg_selftest
    call    print_string

    ; Write 0x55 to data port, read back
    mov     dx, [base_addr]
    add     dx, 0x04        ; Data port
    mov     al, 0x55
    out     dx, al

    ; Small delay for ESP32 ISR processing
    call    io_delay

    in      al, dx
    cmp     al, 0x55
    jne     .selftest_fail

    ; Write 0xAA, read back
    mov     al, 0xAA
    out     dx, al
    call    io_delay
    in      al, dx
    cmp     al, 0xAA
    jne     .selftest_fail

    mov     dx, msg_ok
    call    print_string
    call    print_newline

    ; ---- Step 3: Full 256-byte loopback test ----
    mov     dx, msg_loopback
    call    print_string

    xor     cx, cx          ; CX = test byte counter (0-255)
    xor     bx, bx          ; BX = error counter

.loop_test:
    ; Write test byte
    mov     dx, [base_addr]
    add     dx, 0x04
    mov     al, cl
    out     dx, al

    ; Delay for ESP32 ISR
    call    io_delay

    ; Read back
    in      al, dx

    ; Compare
    cmp     al, cl
    je      .loop_match

    ; Mismatch! Save the bad byte in AH before prints clobber AL.
    mov     ah, al

    ; Count it
    inc     bx
    cmp     bx, 10          ; Only report first 10 errors
    ja      .loop_skip_report

    push    cx
    push    bx
    push    ax              ; Save AH (bad byte) + AL (also bad byte)
    mov     dx, msg_mismatch
    call    print_string
    mov     al, cl          ; Expected
    call    print_hex8
    mov     dx, msg_got
    call    print_string
    pop     ax              ; Restore: AH = bad byte
    mov     al, ah          ; Move bad byte to AL for printing
    call    print_hex8
    call    print_newline
    pop     bx
    pop     cx

.loop_skip_report:
.loop_match:
    inc     cl
    jnz     .loop_test      ; Loop 0-255, wraps to 0

    ; ---- Step 4: Report results ----
    call    print_newline
    cmp     bx, 0
    jne     .test_fail

    ; All passed
    mov     dx, msg_pass
    call    print_string
    mov     al, 0
    jmp     .exit

.test_fail:
    mov     dx, msg_fail
    call    print_string
    mov     ax, bx
    call    print_dec16
    mov     dx, msg_errors
    call    print_string
    mov     al, 2
    jmp     .exit

.no_card:
    mov     dx, msg_nocard
    call    print_string
    mov     al, 1
    jmp     .exit

.no_card_stuck:
    mov     dx, msg_nocard_stuck
    call    print_string
    mov     al, 1
    jmp     .exit

.selftest_fail:
    ; AL contains the bad value
    push    ax
    mov     dx, msg_selftest_fail
    call    print_string
    pop     ax
    call    print_hex8
    call    print_newline
    mov     al, 2
    jmp     .exit

.exit:
    ; Exit with error code in AL
    mov     ah, 0x4C
    int     0x21

; ============================================================================
; SUBROUTINES
; ============================================================================

; Print string at DS:DX (terminated by '$')
print_string:
    push    ax
    mov     ah, 0x09
    int     0x21
    pop     ax
    ret

; Print newline (CR+LF)
print_newline:
    push    ax
    push    dx
    mov     dl, 0x0D
    mov     ah, 0x02
    int     0x21
    mov     dl, 0x0A
    int     0x21
    pop     dx
    pop     ax
    ret

; Print single character in AL
print_char:
    push    dx
    push    ax
    mov     dl, al
    mov     ah, 0x02
    int     0x21
    pop     ax
    pop     dx
    ret

; Print AL as 2-digit hex
print_hex8:
    push    ax
    push    cx
    mov     cl, al
    shr     al, 4
    call    .nib
    mov     al, cl
    and     al, 0x0F
    call    .nib
    pop     cx
    pop     ax
    ret
.nib:
    add     al, '0'
    cmp     al, '9'
    jbe     .nib_ok
    add     al, 7
.nib_ok:
    call    print_char
    ret

; Print AX as 4-digit hex
print_hex16:
    push    ax
    mov     al, ah
    call    print_hex8
    pop     ax
    call    print_hex8
    ret

; Print AL as decimal (0-255)
print_dec8:
    push    ax
    push    bx
    push    cx
    push    dx
    xor     ah, ah
    mov     bl, 100
    div     bl          ; AL=hundreds, AH=remainder
    or      al, al
    jz      .skip_h
    add     al, '0'
    call    print_char
.skip_h:
    mov     al, ah
    xor     ah, ah
    mov     bl, 10
    div     bl          ; AL=tens, AH=ones
    add     al, '0'
    call    print_char
    mov     al, ah
    add     al, '0'
    call    print_char
    pop     dx
    pop     cx
    pop     bx
    pop     ax
    ret

; Print AX as unsigned decimal
print_dec16:
    push    ax
    push    bx
    push    cx
    push    dx
    mov     cx, 0
    mov     bx, 10
.dec_loop:
    xor     dx, dx
    div     bx
    push    dx
    inc     cx
    or      ax, ax
    jnz     .dec_loop
.dec_print:
    pop     dx
    add     dl, '0'
    mov     ah, 0x02
    int     0x21
    loop    .dec_print
    pop     dx
    pop     cx
    pop     bx
    pop     ax
    ret

; I/O delay: courtesy only -- IOCHRDY handles bus timing for non-cached reads.
; ~50us on 8088, faster on faster CPUs.
io_delay:
    push    cx
    mov     cx, 100
.delay_loop:
    in      al, 0x61    ; Read port B (safe, always exists)
    loop    .delay_loop
    pop     cx
    ret

; Parse /A:hhh from command line
parse_args:
    push    ax
    push    bx
    push    cx
    push    si

    mov     si, 0x81        ; PSP command tail
    mov     cl, [0x80]      ; Command tail length
    xor     ch, ch

.scan:
    cmp     cx, 0
    je      .parse_done
    lodsb
    dec     cx
    cmp     al, '/'
    jne     .scan
    ; Found '/', check for 'A' or 'a'
    cmp     cx, 0
    je      .parse_done
    lodsb
    dec     cx
    or      al, 0x20        ; To lowercase
    cmp     al, 'a'
    jne     .scan
    ; Found '/A', expect ':'
    cmp     cx, 0
    je      .parse_done
    lodsb
    dec     cx
    cmp     al, ':'
    jne     .scan
    ; Read hex digits
    xor     bx, bx
.hex_digit:
    cmp     cx, 0
    je      .hex_done
    lodsb
    dec     cx
    ; Convert hex digit
    cmp     al, '0'
    jb      .hex_done
    cmp     al, '9'
    jbe     .hex_09
    or      al, 0x20
    cmp     al, 'a'
    jb      .hex_done
    cmp     al, 'f'
    ja      .hex_done
    sub     al, 'a' - 10
    jmp     .hex_add
.hex_09:
    sub     al, '0'
.hex_add:
    shl     bx, 4
    xor     ah, ah
    add     bx, ax
    jmp     .hex_digit
.hex_done:
    cmp     bx, 0
    je      .parse_done
    mov     [base_addr], bx

.parse_done:
    pop     si
    pop     cx
    pop     bx
    pop     ax
    ret

; ============================================================================
; DATA
; ============================================================================

section .data

base_addr:  dw  0x280       ; Default base address

msg_banner: db  '============================================', 0x0D, 0x0A
            db  ' NetISA Phase 0 - Bus Loopback Test', 0x0D, 0x0A
            db  '============================================', 0x0D, 0x0A, '$'
msg_base:   db  'Base address: 0x$'
msg_detect: db  'Detecting card... $'
msg_found:  db  'Found! Status=0x$'
msg_nocard: db  'NOT FOUND (bus returned 0xFF).', 0x0D, 0x0A
            db  'Check: card seated? address jumpers? slot power?', 0x0D, 0x0A, '$'
msg_nocard_stuck:
            db  'NOT FOUND (bus returned 0x00, BOOT flag clear).', 0x0D, 0x0A
            db  'Check: CPLD programmed? ESP32 booted? PBOOT wired?', 0x0D, 0x0A, '$'
msg_noboot: db  'WARNING: BOOT_COMPLETE flag not set. Card may still be booting.', 0x0D, 0x0A, '$'
msg_fwver:  db  'Firmware: v$'
msg_selftest: db 'Bus self-test (0x55/0xAA)... $'
msg_ok:     db  'OK$'
msg_selftest_fail: db 'FAILED! Got: 0x$'
msg_loopback: db 'Loopback test (256 bytes)...', 0x0D, 0x0A, '$'
msg_mismatch: db '  MISMATCH: wrote 0x$'
msg_got:    db  ' got 0x$'
msg_pass:   db  'PASS: All 256 bytes verified.', 0x0D, 0x0A, '$'
msg_fail:   db  'FAIL: $'
msg_errors: db  ' byte(s) mismatched.', 0x0D, 0x0A, '$'
