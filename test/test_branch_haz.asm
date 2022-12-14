.set noreorder
addi $t0, $t0, 12
addi $t1, $t1, 12
beq $t0, $t1, 8     # execute delay slot as well
addi $t2, $t2, 1024
addi $t5, $t5, 2048
addi $t3, $t3, 16
add $t4, $t4, 256
.word 0xfeedfeed