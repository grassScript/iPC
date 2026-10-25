[bits 16]
[cpu 386]
[org 0x7c00]

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00

    ; Read kernel (128 sectors = 64KB) to 0x10000
    mov si, dap
    mov ah, 0x42
    int 0x13
    jc halt

    ; Query VBE info
    mov dword [0x8000], 'VBE2'
    mov ax, 0x4F00
    mov di, 0x8000            ; VbeInfoBlock
    int 0x10
    cmp ax, 0x004F
    jne vbe_fallback

    ; Walk mode list
    mov si, [0x800E]          ; mode list offset
    mov ax, [0x8010]          ; mode list segment
    mov fs, ax
    xor eax, eax
    mov [best_area], eax
    mov word [best_mode], 0

.vbe_loop:
    mov ax, fs:[si]
    add si, 2
    cmp ax, 0xFFFF
    je .vbe_select
    mov [cur_mode], ax
    mov [list_si], si

    mov cx, ax
    mov ax, 0x4F01
    mov di, 0x8200            ; ModeInfoBlock
    int 0x10
    mov si, [list_si]
    cmp ax, 0x004F
    jne .vbe_loop

    mov ax, [0x8200]          ; attributes
    and ax, 0x0091            ; supported | linear FB | LFB available
    cmp ax, 0x0091
    jne .vbe_loop
    cmp byte [0x8219], 32     ; BitsPerPixel == 32
    jne .vbe_loop
    cmp byte [0x821B], 6      ; direct color
    jne .vbe_loop

    movzx eax, word [0x8212]  ; XResolution
    movzx ebx, word [0x8214]  ; YResolution
    cmp eax, 640
    jb .vbe_loop
    cmp ebx, 480
    jb .vbe_loop
    cmp eax, 1024
    ja .vbe_loop
    cmp ebx, 768
    ja .vbe_loop
    imul eax, ebx
    cmp eax, [best_area]
    jbe .vbe_loop
    mov [best_area], eax
    mov ax, [cur_mode]
    mov [best_mode], ax
    jmp .vbe_loop

.vbe_select:
    mov ax, [best_mode]
    test ax, ax
    jnz vbe_set_mode

vbe_fallback:
    mov ax, 0x144             ; fallback: 1024x768x32
    mov [best_mode], ax

vbe_set_mode:
    mov cx, ax
    mov ax, 0x4F01
    mov di, 0x8200
    int 0x10
    cmp ax, 0x004F
    jne halt

    ; Fill boot_info_t at 0x9000
    mov eax, [0x8228]         ; PhysBasePtr
    mov [0x9000], eax
    movzx eax, word [0x8210]  ; BytesPerScanLine
    mov [0x9004], eax
    movzx eax, word [0x8212]  ; XResolution
    mov [0x9008], eax
    movzx eax, word [0x8214]  ; YResolution
    mov [0x900c], eax
    movzx eax, byte [0x8219]  ; BitsPerPixel
    mov [0x9010], eax
    movzx eax, word [best_mode]
    mov [0x9014], eax         ; Mode number

    mov bx, [best_mode]
    or bx, 0x4000             ; linear framebuffer
    mov ax, 0x4F02
    int 0x10
    cmp ax, 0x004F
    jne halt

    ; A20 line
    in al, 0x92
    or al, 2
    out 0x92, al

    ; Enter 32-bit protected mode
    lgdt [gdt_desc]
    mov eax, cr0
    or al, 1
    mov cr0, eax
    jmp 0x08:pm_entry

halt:
    hlt
    jmp halt

cur_mode:   dw 0
best_mode:  dw 0
list_si:    dw 0
best_area:  dd 0

dap:
    db 0x10, 0
    dw 128
    dw 0x0000, 0x1000
    dq 1

gdt_start:
    dq 0
gdt_code:
    dw 0xffff, 0x0000, 0x9a00, 0x00cf
gdt_data:
    dw 0xffff, 0x0000, 0x9200, 0x00cf
gdt_end:

gdt_desc:
    dw gdt_end - gdt_start - 1
    dd gdt_start

[bits 32]
pm_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000

    push 0x9000
    call 0x10000

    cli
    hlt

times 510-($-$$) db 0
dw 0xaa55
