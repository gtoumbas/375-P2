.set noreorder
la $s0, next+4 
la $s1, next+8 
la $s2, next+12 
# Load initial values 
lw $s3, 0($s0) 
lw $s4, 0($s1)
lw $s5, 0($s2) 

# Sw test
lui $t0, 0x1234 # t0 = 0x12340000
sw $t0, 0($s0)
lw $t4, 0($s0) # t4 should equal 0x12340000
# Sh test
lui $t1, 0xabcd # t1 = 0xabcd0000
sh $t1, 0($s1)
lhu $t5, 0($s1) # t5 should equal 0xabcd
# Sb test
lui $t2, 0x4567 # t2 = 0x45670000
sb $t2, 0($s2)
lbu $t6, 0($s2) # t6 should equal 0x45
next:
.word 0xfeedfeed
.word 0xffffffff
.word 0xffffffff
.word 0xffffffff

