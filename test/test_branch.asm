.set noreorder
addi $t0, $t0, 12
addi $t1, $t1, 12
beq $t0, $t1, 8
addi $t2, $t2, 421
addi $t3, $t3, 3252
sub $t0, $t0, $t0
sub $t1, $t1, $t1
addi $t0, $t0, 10
bne $t0, $t1, 4
addi $t1, $t1, 4214
addi $t3, $t3, 16
.word 0xfeedfeed