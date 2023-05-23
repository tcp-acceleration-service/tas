#!/usr/bin/gnuplot -persist
set terminal pdf font "Latin Modern Roman"
set output "plot.pdf"
set key autotitle columnhead
set key left top
set logscale x 2
              
set key top left
set xlabel "Message Size [Bytes]"
set ylabel "Throughput [Mbps]"
set yrange [0:]

plot 'tp.dat' using 1:2:6 with yerrorlines title 'tas', \
     'tp.dat' using 1:3:7 with yerrorlines title 'bare-virtuoso', \
     'tp.dat' using 1:4:8 with yerrorlines title 'virtuoso', \
     'tp.dat' using 1:5:9 with yerrorlines title 'tunoff-virtuoso', \
     # 'tp.dat' using 1:5:9 with yerrorlines title 'ovs-linux' linetype 5 ps 0.7, \
     # 'tp.dat' using 1:6:11 with yerrorlines title 'ovs-tas' linetype 6 ps 0.7, \
