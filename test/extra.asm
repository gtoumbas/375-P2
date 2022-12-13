# The following lines are used to test out more instructions
main: jal   jmp               # set pc to next address and $ra to the next address
jmp:  addi  $ra, $ra, 8       # add 8 to $ra so that when jr is called, pc is set to the next address
      jr    $ra               # set pc to the next address
      la    $s0, 116          # address of binary value 1100 1010 1111 0110
      la    $s1, 120          # address of checkpoint
      lbu   $s2, 2($s0)       # $s2 = 1100 1010 = 0xca = 202
      lbu   $s3, 3($s0)       # $s3 = 1111 0110 = 0xf6 = 246
      and   $t0, $s2, $s3     # $t0 = 1100 0010 = 0xc2 = 194
      andi  $t1, $s2, 65465   # $t1 = 1000 1000
      andi  $t2, $s2, 185     # $t2 = 1000 1000
      beq   $t1, $t2, cp      # goto checkpoint (the 8 least significant bits of 185 and 65465 = 1011 1001)
rtn1: sll   $t0, $t0, 8       # $t0 = 1100 0010 0000 0000 = 0xc200
      or    $t0, $t0, $t1     # $t0 = 1100 0010 1000 1000 = 0xc288
      sh    $t0, 0($s0)       # value at address $s0 now equals 0xc288caf6
      lui   $t0, 36079        # $t0 = (unsigned) 36079 * 2^16 = (signed) -29457 = 0x8cef0000 = 1000 1100 1110 1111 0000 0000 0000 0000
      srl   $t1, $t0, 16      # $t1 = 36079 = 0x8cef = 1000 1100 1110 1111
      slt   $t2, $t0, $t1     # $t2 = 1
      sltu  $t3, $t1, $t0     # $t3 = 1
      sltiu $t4, $t3, -1      # $t4 = 1 (-1 = 0xffff = (unsigned) 65535)
      subu  $t5, $t0, $t1     # $t5 = (unsigned) 2364437265 = 0x8cee7311 = 1000 1100 1110 1110 0111 0011 0001 0001  
      sh    $t5, 0($s1)       # checkpoint now equals 0x73118080
      .word 0xfeedfeed        # end of program
cp:   addiu $t6, $zero, 32896 # $t6 = 0xffff8080
      sh    $t6, 2($s1)       # set checkpoint to 0xffff8080
      j     rtn1              # return to branching instruction

rsl:  .word 0xcaf6            # value: 51958 = 1100 1010 1111 0110
      .word 0x0               # checkpoint
