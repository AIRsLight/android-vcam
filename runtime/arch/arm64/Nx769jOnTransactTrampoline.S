    .section .text.vcam_nx769j_on_transact_trampoline,"ax",@progbits
    .p2align 4
    .global vcam_nx769j_on_transact_trampoline
    .hidden vcam_nx769j_on_transact_trampoline
    .type vcam_nx769j_on_transact_trampoline, %function
vcam_nx769j_on_transact_trampoline:
    // Indirect calls from the pass-through bridge must land on BTI C.
    .inst 0xd503245f

    // Load the one-shot resume address before executing the OEM prologue.
    // x16/x17 are AAPCS64 intra-procedure scratch registers and none of the
    // four reviewed NX769J instructions consumes them.
    adrp x16, vcam_nx769j_on_transact_resume_address
    add x16, x16, :lo12:vcam_nx769j_on_transact_resume_address
    ldar x17, [x16]
    cbz x17, .Lvcam_nx769j_unbound

    // Exact CameraService::onTransact prologue from the allowlisted OEM ELF.
    .inst 0xd503233f  // paciasp
    .inst 0xd10283ff  // sub sp, sp, #0xa0
    .inst 0xa9047bfd  // stp x29, x30, [sp, #0x40]
    .inst 0xa9056ffc  // stp x28, x27, [sp, #0x50]
    br x17

.Lvcam_nx769j_unbound:
    mov w0, #-38      // -ENOSYS; no OEM prologue has executed yet.
    ret

    .global vcam_nx769j_on_transact_trampoline_end
    .hidden vcam_nx769j_on_transact_trampoline_end
vcam_nx769j_on_transact_trampoline_end:
    .size vcam_nx769j_on_transact_trampoline, .-vcam_nx769j_on_transact_trampoline

    .section .note.GNU-stack,"",@progbits
