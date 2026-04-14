; hello.asm - Trivial test program for relay validation
; Build: nasm -f bin -o hello.com hello.asm
[BITS 16]
[ORG 0x100]
    mov     dx, msg
    mov     ah, 09h
    int     21h
    mov     ax, 4C00h   ; Exit with code 0
    int     21h
msg: db 'Hello from DOS! Relay works.', 0x0D, 0x0A, '$'
