.set noreorder
addi $s1, $s2, 1 # Works
addiu $s2, $s3, 2 # Works 
andi $s3, $s2, 0xffff # s3 = 2 & 0xffff = 2 Works
la $s4, 68 # addr of 0x1234
la $s5, 72 # addr of 0xabcd
la $s6, 76 # addr of 0xbaba
lbu $t0, 0($s4) # 
lhu $t1, 0($s5) # 
lui $t3, 0x1234 # t3 = 0x12340000
lw $t4, 0($s6) # t4 = 0xbaba
ori $t5, $t6, 0xffff
slti $t6, $t7, 0x0
sltiu $t7, $t8, 10
# set values of s4, s5, s6
sb $t3, 0($s4) 
sh $t0, 0($s5)
sw $t3, 0($s6)
.word 0xfeedfeed
.word 0x12345678
.word 0xabcdabcd
.word 0xbabababa
