#!/usr/bin/gnuplot -persist
set terminal pdf
set output "plot.pdf"
set colorsequence podo
set key autotitle columnhead
set key left top
set logscale x 2
set bmargin 4
              
set key top right
set label 1 "Aggressor Client # Connections" at screen 0.5, 0.03 center
set label 2 "Victim Client Throughput (Mbps)" at screen 0.01, 0.5 rotate by 90 center

set yrange [0:2000]
plot 'tp.dat' using 1:2:xtic(1) title 'bare-tas' linetype 2 w lp, \
     'tp.dat' using 1:4:xtic(1) title 'virt-tas' linetype 4 w lp, \
     'tp.dat' using 1:5:xtic(1) title 'ovs-linux' linetype 5 w lp, \
     'tp.dat' using 1:6:xtic(1) title 'ovs-tas' linetype 6 w lp, \
