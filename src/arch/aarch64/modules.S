// SPDX-License-Identifier: MIT
/*
 * src/arch/aarch64/modules.S - Embedded binary blobs for built-in kernel modules and assets
 */
.section .modules, "a"

.global _mod_storage_server_start
.global _mod_storage_server_end
_mod_storage_server_start:
.incbin "Asm/boot/storage_server.qex"
_mod_storage_server_end:

.global _mod_input_server_start
.global _mod_input_server_end
_mod_input_server_start:
.incbin "Asm/boot/input_server.qex"
_mod_input_server_end:

.global _mod_time_server_start
.global _mod_time_server_end
_mod_time_server_start:
.incbin "Asm/boot/time_server.qex"
_mod_time_server_end:

.global _mod_comp_start
.global _mod_comp_end
_mod_comp_start:
.incbin "Asm/boot/comp.qex"
_mod_comp_end:

.global _mod_terminal_start
.global _mod_terminal_end
_mod_terminal_start:
.incbin "Asm/boot/terminal.qex"
_mod_terminal_end:

.global _mod_ttysh_start
.global _mod_ttysh_end
_mod_ttysh_start:
.incbin "Asm/boot/ttysh.qex"
_mod_ttysh_end:

.global _mod_init_start
.global _mod_init_end
_mod_init_start:
.incbin "Asm/boot/init.qex"
_mod_init_end:

.global _mod_font_atlas_start
.global _mod_font_atlas_end
_mod_font_atlas_start:
.incbin "Asm/boot/font_atlas.bin"
_mod_font_atlas_end:

.global _mod_icons_start
.global _mod_icons_end
_mod_icons_start:
.incbin "Asm/boot/icons.bin"
_mod_icons_end:

.global _mod_wallpaper_start
.global _mod_wallpaper_end
_mod_wallpaper_start:
.incbin "Asm/boot/wallpaper.bin"
_mod_wallpaper_end:

.global _mod_logo_start
.global _mod_logo_end
_mod_logo_start:
.incbin "Asm/boot/logo.bmp"
_mod_logo_end:
