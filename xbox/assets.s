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
asset cell_light
asset ambience
asset gpu_vertices
asset bot_poses
asset gpu_textures
asset knife
asset usp
asset pistol_reload
asset auto
asset auto_reload
asset sub
asset sub_reload
asset clip_empty
asset ric1
asset ric2
asset ric3
asset waterimpact
asset jump
asset land
asset step
asset footsteps
asset crouch
asset watersteps
asset watercrouch
asset crouch_in
asset crouch_out
asset pain1
asset pain2
asset pain3
asset pain4
asset pain5
asset pain6
asset gib
asset die1
asset die2
asset splash1
asset splash2
asset pickup_ammo_clip
asset pickup_health
asset pickup_helmet
asset pickup_armour
asset change_gun
.set "@feat.00", 1
