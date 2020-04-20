#!/bin/bash                                                                                                                                                  
                                                                                                                                                            
P1=$((1<<0))                                                                                                                                                 
P2=$((1<<1))                                                                                                                                                 
P3=$((1<<2))                                                                                                                                                 
P4=$((1<<3))                                                                                                                                                 
P5=$((1<<4))                                                                                                                                                 
                                                                                                                                                            
for i in $P1 $P2 $P3 $P4 $P5; do                                                                                                                             
    mkdir tmp-$i 
    cd tmp-$i                                                                                                                                                
    mkdir normal                                                                                                                                             
    mkdir flushopt                                                                                                                                           
    cd ..                                                                                                                                                    
    cp durable_queue_normal tmp-$i/normal                                                                                                                    
    cp durable_queue_flushopt tmp-$i/flushopt                                                                                                                
    cp durable_queue_verifier_normal tmp-$i/normal                                                                                                           
    cp durable_queue_verifier_flushopt tmp-$i/flushopt                                                                                                       
    sbatch -o $i.out -e $i.err -c 8 -J pmat-$i --mem=8G --mail-type=BEGIN,END --mail-user=ljenkin4@ur.rochester.edu -N 1 -t 24:00:00 --wrap="                
    cd /home/ljenkin4/Persistent-Memory-Analysis-Tool/pmat/tests/microbenchmarks/durable_queue/tmp-$i/normal                                                 
    for sched in 'yes' 'no' 'random'; do                                                                                                                     
        for sample in \`seq 1 100\`; do                                                                                                                      
            /home/ljenkin4/Persistent-Memory-Analysis-Tool/bin/valgrind --tool=pmat --fair-sched=\$sched \                                                   
            --terminate-on-error=yes  --verifier=durable_queue_verifier_normal \                                                                             
            ./durable_queue_normal 60 &> \"bug-\$sched.$i.\$sample.out\"                                                                                     
        done;                                                                                                                                                
    done;                                                                                                                                                    
    mv *.out ../../                                                                                                                                          
    cd ..                                                                                                                                                    
    rm -rf normal                                                                                                                                            
    "                                                                                                                                                        
    sbatch -o $i.out -e $i.err -c 8 -J pmat-$i --mem=8G --mail-type=BEGIN,END --mail-user=ljenkin4@ur.rochester.edu -N 1 -t 24:00:00 --wrap="                
    cd /home/ljenkin4/Persistent-Memory-Analysis-Tool/pmat/tests/microbenchmarks/durable_queue/tmp-$i/flushopt                                               
    for sched in 'yes' 'no' 'random'; do                                                                                                                     
        for sample in \`seq 1 100\`; do                                                                                                                      
            /home/ljenkin4/Persistent-Memory-Analysis-Tool/bin/valgrind --tool=pmat --fair-sched=\$sched \                                                   
            --terminate-on-error=yes  --verifier=durable_queue_verifier_flushopt \                                                                           
            ./durable_queue_flushopt 60 &> \"bug_flushopt-\$sched.$i.\$sample.out\"                                                                          
        done;                                                                                                                                                
    done;                                                                                                                                                    
    mv *.out ../../                                                                                                                                          
    cd ..                                                                                                                                                    
    rm -rf flushopt                                                                                                                                          
    "                                                                                                                                                        
done;  