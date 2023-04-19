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
plot 'tp.dat' using 1:2:6 with yerrorlines title 'tas' linetype 2 ps 0.7, \
     'tp.dat' using 1:3:7 with yerrorlines title 'bare-virtuoso' linetype 3 ps 0.7, \
     'tp.dat' using 1:4:8 with yerrorlines title 'virtuoso' linetype 4 ps 0.7, \
     'tp.dat' using 1:5:9 with yerrorlines title 'ovs-linux' linetype 5 ps 0.7, \
     # 'tp.dat' using 1:6:11 with yerrorlines title 'ovs-tas' linetype 6 ps 0.7, \
