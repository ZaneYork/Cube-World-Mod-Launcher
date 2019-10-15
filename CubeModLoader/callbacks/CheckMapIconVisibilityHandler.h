int CheckMapIconVisibilityHandler(void* player, int a2, int a3) {
    for (DLL* dll: modDLLs) {
        if (dll->HandleCheckMapIconVisibility) {
            if ( int result = ((int(*)(void*, int, int))dll->HandleCheckMapIconVisibility)(player, a2, a3)  ){
                return result;
            }
        }
    }
    return 0;
}
void* CheckMapIconVisibilityHandler_ptr = (void*)&CheckMapIconVisibilityHandler;

void* ASMCheckMapIconVisibilityHandler_jmpback;
void* ASMCheckMapIconVisibilityHandler_ret0;
void* ASMCheckMapIconVisibilityHandler_ret1;
void no_optimize ASMCheckMapIconVisibilityHandler() {
    asm(PUSH_ALL

        PREPARE_STACK
        "call [CheckMapIconVisibilityHandler_ptr] \n"

        RESTORE_STACK

        // Did the handler return 1? true
        "cmp eax, 1 \n"
        "je 1f \n"

        // Otherwise? Do nothing
        POP_ALL

        // original code
        "mov     rdi, [rcx+0x1528] \n"
        "mov     esi, r8d \n"
        "mov     ebp, edx \n"
        "test    rdi, rdi \n"
        "jmp [ASMCheckMapIconVisibilityHandler_jmpback] \n"


        "1: \n"  //not
        POP_ALL
        "jmp [ASMCheckMapIconVisibilityHandler_ret1] \n"

       );
}
void SetupCheckMapIconVisibilityHandler() {
    WriteFarJMP(base+0x5F4DE, (void*)&ASMCheckMapIconVisibilityHandler);
    ASMCheckMapIconVisibilityHandler_jmpback = (void*)base+0x5F4ED;
    ASMCheckMapIconVisibilityHandler_ret1 = (void*)base+0x5F6B4;
}
