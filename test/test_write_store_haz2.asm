.set noreorder
addi $t0, $t0, next + 4 # t0 = next + 4 = 0x1c
addi $t1, $t1, next + 4 # t1 = next + 4 = 0x1c
lw $t2, 0($t1)          # t2 = 0x10             forward next + 4 from line 3
subi $t2, $t2, 4
addi $t0, $t0, 4        # t0 = next + 8 = 0x20
addi $t2, $t2, 4
sw $t2, 0($t0)          # M[next + 8] = {0x0 ---> 0x10}
lw $t3, 0($t0)          # t3 = 0x10
next:                   # next = 24 = 0x18
.word 0xfeedfeed
.word 0x10
.word 0x0