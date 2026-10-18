global switch_task

switch_task:
    mov eax, [esp + 4]
    mov edx, [esp + 8]

    push ebp
    push edi
    push esi
    push edx
    push ecx
    push ebx

    mov [eax], esp
    mov esp, [edx]

    pop ebx
    pop ecx
    pop edx
    pop esi
    pop edi
    pop ebp

    ret
