.set noreorder
addi $t0, $t0, 12           # t0 = 12
addi $t1, $t1, 12           # t1 = 12
beq $t0, $t1, 8             # true
addi $t2, $t2, 1024         # delaySlot: t2 = 1024 = 0x400
addi $t3, $t3, 4096         #...
sub $t0, $t0, $t0           # t0 = 0
sub $t1, $t1, $t1           # t1 = 0
addi $t0, $t0, 10           # t0 = 10
bne $t0, $t1, 4             # true
addi $t1, $t1, 4214         # delaySlot: t1 = 4214 = 0x1076
addi $t3, $t3, 16           # t3 = 16 = 0xa
.word 0xfeedfeed