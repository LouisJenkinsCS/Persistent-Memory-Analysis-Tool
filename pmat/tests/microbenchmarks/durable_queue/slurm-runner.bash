#!/bin/bash                                                                                                                                                  
                                                                                                                                                            
for i in `seq 0 10`; do                                                                                                                                      
    # Parallelize outer loop                                                                                                                                 
    sbatch -o $i.out -e $i.err -c 16 -J pmat-$i --mem=8G --mail-type=BEGIN,END --mail-user=ljenkin4@ur.rochester.edu -N 1 -t 24:00:00 --wrap="               
    for j in \`seq 0 10\`; do                                                                                                                                
        for sched in 'no' 'yes' 'random'; do                                                                                                                 
            for quantum in 1 10 100 1000 10000 100000; do                                                                                                    
                for randomize in 'yes'; do                                                                                                                   
                    cacheSize=$((1<<$i))                                                                                                                     
                    wbSize=\$((1<<\$j))                                                                                                                      
                    fname=\"cacheSize=\$cacheSize-wbSize=\$wbSize-sched=\$sched-quantum=\$quantum-randomize=\$randomize\"                                    
                    echo \"Launching \$fname\"                                                                                                               
                    cd /home/ljenkin4/Persistent-Memory-Analysis-Tool/pmat/tests/microbenchmarks/durable_queue                                               
                    mkdir \$fname.dir                                                                                                                        
                    cd \$fname.dir                                                                                                                           
                    /home/ljenkin4/Persistent-Memory-Analysis-Tool/bin/valgrind \                                                                            
                        --fair-sched=\$sched \                                                                                                               
                        --tool=pmat \                                                                                                                        
                        --aggregate-dump-only=yes \                                                                                                          
                        --verifier=../durable_queue_verifier \                                                                                               
                        --num-wb-entries=\$wbSize \                                                                                                          
                        --num-cache-entries=\$cacheSize \                                                                                                    
                        --randomize-quantum=\$randomize \                                                                                                    
                        --scheduling-quantum=\$quantum \                                                                                                     
                        ../durable_queue 60 &> \$fname.out                                                                                                   
                    cp \$fname.out ../                                                                                                                       
                    cd ..                                                                                                                                    
                    rm -rf \$fname.dir                                                                                                                       
                done                                                                                                                                         
            done                                                                                                                                             
        done                                                                                                                                                 
    done"                                                                                                                                                    
done