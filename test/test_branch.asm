.set noreorder
addi $t0, $t0, 16           # t0 = 16 = 0x10
addi $t1, $t1, 16           # t1 = 16 = 0x10
beq $t0, $t1, 8             # true
addi $t2, $t2, 1024         # delaySlot: t2 = 1024 = 0x400
addi $t3, $t3, 4096         #...
sub $t0, $t0, $t0           # t0 = 0
sub $t1, $t1, $t1           # t1 = 0
addi $t0, $t0, 10           # t0 = 10 = 0xa
bne $t0, $t1, 4             # true
addi $t1, $t1, 4214         # delaySlot: t1 = 4214 = 0x1076
addi $t3, $t3, 16           # t3 = 4096 + 16 = 0x1010
.word 0xfeedfeed