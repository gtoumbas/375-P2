# A simple test case that includes a load-use stall and a branch.
.set noreorder
addi $t4, $zero, next+4; #t4 = 24
lw $t0, 0($t4);          #t0 = 0x24
addi $t2, $t0, 0x44;     #t2 = 0x68 
bne $t2, $t0, next;
addi $t1, $zero, 300;
sll  $t1, $t1, 16
next:
.word 0xfeedfeed
.word 0x24
