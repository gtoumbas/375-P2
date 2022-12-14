.set noreorder
# Test illegal instruction (store conditional)
sc $t1, $t2, 0($t3)
.word 0xfeedfeed # Program should not stop as it goes to exc address