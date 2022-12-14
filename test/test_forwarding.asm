.set noreorder

# Test load immediate followed by store immediate
la $t1, data+4
li $t0, 0x12345678
sw $t0, 0($t1)  # store 0x12345678 to data+4, should trigger forwarding

# Test load immediate followed by branching 
li $t2, 0x12345678
beq $t2, $t2, firstbranch
nop
li $t3, 0xdeadbeef # should not be executed
firstbranch:

# Test adding followed by branching
li $t4, 0x00000001
li $t5, 0x00000002
add $t6, $t4, $t5
beq $t6, $t5, secondbranch
nop
li $t7, 0xdeadbeef # should not be executed
secondbranch:

data:
.word 0xfeedfeed
.word 0xffffffff
