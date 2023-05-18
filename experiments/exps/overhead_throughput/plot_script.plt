#!/usr/bin/gnuplot -persist
set terminal pdf
set output "plot.pdf"
set key autotitle columnhead
set key left top
set logscale x 2
set bmargin 4
              
set key top right
set label 1 "Message Size [Bytes]" at screen 0.5, 0.03 center
set label 2 "Throughput [Mbps]" at screen 0.01, 0.5 rotate by 90 center

plot 'tp.dat' using 1:2:5 with yerrorlines title 'tas' ps 0.7, \
     'tp.dat' using 1:3:6 with yerrorlines title 'bare-virtuoso' ps 0.7, \
     'tp.dat' using 1:4:7 with yerrorlines title 'virtuoso' ps 0.7, \
     # 'tp.dat' using 1:5:9 with yerrorlines title 'ovs-linux' linetype 5 ps 0.7, \
     # 'tp.dat' using 1:6:11 with yerrorlines title 'ovs-tas' linetype 6 ps 0.7, \
