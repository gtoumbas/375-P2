.set noreorder
# TEsts for overflow
li $t0, 0x7fffffff
li $t1, 0x7fffffff
add $t2, $t0, $t1
.word 0xfeedfeed # Program should not stop as it goes to exc address