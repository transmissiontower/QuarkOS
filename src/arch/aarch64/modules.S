.section .modules, "a"

.global _mod_storage_server_start
.global _mod_storage_server_end
_mod_storage_server_start:
.incbin "build/storage_server.qex"
_mod_storage_server_end:

.global _mod_input_server_start
.global _mod_input_server_end
_mod_input_server_start:
.incbin "build/input_server.qex"
_mod_input_server_end:

.global _mod_time_server_start
.global _mod_time_server_end
_mod_time_server_start:
.incbin "build/time_server.qex"
_mod_time_server_end:

.global _mod_comp_start
.global _mod_comp_end
_mod_comp_start:
.incbin "build/comp.qex"
_mod_comp_end:

.global _mod_terminal_start
.global _mod_terminal_end
_mod_terminal_start:
.incbin "build/terminal.qex"
_mod_terminal_end:

.global _mod_ttysh_start
.global _mod_ttysh_end
_mod_ttysh_start:
.incbin "build/ttysh.qex"
_mod_ttysh_end:

.global _mod_init_start
.global _mod_init_end
_mod_init_start:
.incbin "build/init.qex"
_mod_init_end:

.global _mod_font_atlas_start
.global _mod_font_atlas_end
_mod_font_atlas_start:
.incbin "build/font_atlas.bin"
_mod_font_atlas_end:

.global _mod_icons_start
.global _mod_icons_end
_mod_icons_start:
.incbin "build/icons.bin"
_mod_icons_end:

.global _mod_wallpaper_start
.global _mod_wallpaper_end
_mod_wallpaper_start:
.incbin "build/wallpaper.bin"
_mod_wallpaper_end:

.global _mod_logo_start
.global _mod_logo_end
_mod_logo_start:
.incbin "build/logo.bmp"
_mod_logo_end:
