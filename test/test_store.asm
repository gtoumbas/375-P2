.set noreorder
la $s0, next+4 
la $s1, next+8 
la $s2, next+12 
# Load initial values 
lw $s3, 0($s0) 
lw $s4, 0($s1)
lw $s5, 0($s2) 

# Sw test
sw $t0, 0($s0)
next:
.word 0xfeedfeed
.word 0xffffffff
.word 0xffffffff
.word 0xffffffff

