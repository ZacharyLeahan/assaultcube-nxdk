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
asset deltas
asset gpu_vertices
asset gpu_textures
asset usp
asset pistol_reload
asset auto
asset auto_reload
asset jump
asset land
asset step
asset pain1
asset die1
asset pickup_ammo_clip
asset pickup_health
asset pickup_helmet
asset pickup_armour
asset change_gun
.set "@feat.00", 1
