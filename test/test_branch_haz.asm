.set noreorder
addi $t0, $t0, 12
addi $t1, $t1, 12
beq $t0, $t1, 4     # execute delay slot as well
addi $t1, $t1, 4214
addi $t3, $t3, 16
add $t4, $t4, 456
.word 0xfeedfeed