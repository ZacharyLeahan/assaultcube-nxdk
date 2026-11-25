.section .rdata,"dr"
.macro asset name
.p2align 4
.globl _\name
_\name:
.incbin "generated/\name\().bin"
.globl _\name\()_end
_\name\()_end:
.endm
asset world
asset gpu_vertices
asset gpu_textures
asset usp
asset pistol_reload
asset auto
asset auto_reload
.set "@feat.00", 1
