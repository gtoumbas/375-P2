# Need to make sure you have both of these commands in your path 
# Then include name of .arm file without extension e.g bash armToBin fib
mips-linux-gnu-as $1.asm -o $1.elf
mips-linux-gnu-objcopy $1.elf -j .text -O binary $1.bin 
