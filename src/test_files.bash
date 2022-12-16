bin_files=(
    "fib_small.bin" 
    "fib.bin" 
    "test_addi.bin" 
    "test_branch_ex_haz.bin"
    "load_use.bin" 
    "test_branch_haz.bin" 
    "test_branch_load_haz.bin"
    "test_branch_load_haz2.bin"
    "test_branch.bin"
    "test_fwd.bin"
    "test_J.bin"
    "test_lw.bin"
    "test_regs.bin"
    "test_store.bin"
    "test_write_store_haz.bin"
    "test.bin"
)

for bin_file in "${bin_files[@]}"
do

    # Run p1 sim
    echo "Running $bin_file"
    ./solution-sim "../test/$bin_file"

    p1_out=$(cat mem_state.out)
    p1_reg=$(cat reg_state.out)

    ./sim "../test/$bin_file"
    p2_out=$(cat mem_state.out)
    p2_reg=$(cat reg_state.out)


    if [ "$p1_out" == "$p2_out" ];
    then
        # The outputs are the same
        echo "Mem Outputs are the same" 
    else
        # The outputs are different
        echo "Mem Outputs are DIFFERENT"
    fi

    if [ "$p1_reg" == "$p2_reg" ];
    then
        # The outputs are the same
        echo "Reg Outputs are the same" 
    else
        # The outputs are different
        echo "Reg Outputs are DIFFERENT"
    fi
    echo "----------------------------------------"


done