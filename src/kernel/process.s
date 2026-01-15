global switch_context
global enter_user_mode
global irq_ret_stub

switch_context:
    mov eax, [esp + 4]  
    mov edx, [esp + 8]  

    
    push ebp
    push ebx
    push esi
    push edi
    pushf

    mov [eax], esp      
    mov esp, edx        

    popf
    pop edi
    pop esi 
    pop ebx
    pop ebp

    ret 

irq_ret_stub:
    mov ax, 0x23
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    iret

enter_user_mode:
    cli
    mov ax, 0x23
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    mov ebx, [esp + 4] 
    mov ecx, [esp + 8] 

    push 0x23          
    push ecx           

    pushf              
    pop eax            
    or eax, 0x200      
    push eax           

    push 0x1B          
    push ebx           

    iret