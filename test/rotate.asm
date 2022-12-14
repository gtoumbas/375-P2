# Rotates the each value in the array n indices forward (change the value of n to get different results)
main: la    $s0, 80           # load address of array
      la    $s1, 120          # load address of size variable
      lw    $s1, 0($s1)       # load array size
      li    $s2, 3            # load int n
      li    $s3, 1            # holds 1
lp1:  lw    $t0, 0($s0)       # $t0 = temp = arr[0]
      add   $t1, $zero, $s3   # initialize $t1 = i = 1
lp2:  sll   $t2, $t1, 2       # $t2 = i * 4
      add   $t2, $t2, $s0     # $t2 = address of arr[i]
      lw    $t3, 0($t2)       # $t3 = arr[i]
      sw    $t3, -4($t2)      # arr[i - 1] = arr[i]
      addiu $t1, $t1, 1       # increment i
      bne   $t1, $s1, lp2     # continue looping if i != arr.length
      sw    $t0, 0($t2)       # arr[i] = temp (i = arr.length -> $t2 = address of arr[arr.length])
      sub   $s2, $s2, $s3     # decrements n
      bgtz  $s2, lp1          # continue looping if n > 0
      .word 0xfeedfeed        # end of program

rsl:  .word 0x0               # arr[0]
      .word 0x1               # arr[1]
      .word 0x2               # arr[2]
      .word 0x3               # arr[3]
      .word 0x4               # arr[4]
      .word 0x5               # arr[5]
      .word 0x6               # arr[6]
      .word 0x7               # arr[7]
      .word 0x8               # arr[8]
      .word 0x9               # arr[9]
      .word 0xa               # size of "array"
