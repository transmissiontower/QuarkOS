.section .modules, "a"

.global _mod_storage_server_start
.global _mod_storage_server_end
_mod_storage_server_start:
.incbin "build_aarch64_user/storage_server.elf"
_mod_storage_server_end:

.global _mod_input_server_start
.global _mod_input_server_end
_mod_input_server_start:
.incbin "build_aarch64_user/input_server.elf"
_mod_input_server_end:

.global _mod_time_server_start
.global _mod_time_server_end
_mod_time_server_start:
.incbin "build_aarch64_user/time_server.elf"
_mod_time_server_end:

.global _mod_comp_start
.global _mod_comp_end
_mod_comp_start:
.incbin "build_aarch64_user/compositor.elf"
_mod_comp_end:

.global _mod_terminal_start
.global _mod_terminal_end
_mod_terminal_start:
.incbin "build_aarch64_user/terminal.elf"
_mod_terminal_end:

.global _mod_ttysh_start
.global _mod_ttysh_end
_mod_ttysh_start:
.incbin "build_aarch64_user/ttysh.elf"
_mod_ttysh_end:

.global _mod_init_start
.global _mod_init_end
_mod_init_start:
.incbin "build_aarch64_user/init.elf"
_mod_init_end:

.global _mod_logo_start
.global _mod_logo_end
_mod_logo_start:
.incbin "build/logo.bmp"
_mod_logo_end:
