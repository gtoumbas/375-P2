.set noreorder
j target1
nop
end:
addi $t1, $zero, 1 # $t1 = 1
.word 0xfeedfeed
target1:
addi $t0, $zero, 1 # $t0 = 1
jal end
nop

# Progam should update $t0 and $t1 to 1 and stop 
# $ra should be updated as well

