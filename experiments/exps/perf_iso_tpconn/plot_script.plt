#!/usr/bin/gnuplot -persist
set terminal pdf font "Latin Modern Roman"
set output "plot.pdf"
set key autotitle columnhead
set key left top
              
set key center right
set xlabel "Aggressor Client # Connections"
set ylabel "Victim Client Throughput (Mbps)"

set yrange [0:]
plot 'tp.dat' using 1:3:7 with yerrorlines title 'virtuoso', \
     'tp.dat' using 1:2:6 with yerrorlines title 'tas', \
     'tp.dat' using 1:4:8 with yerrorlines title 'ovs-linux', \
     'tp.dat' using 1:5:9 with yerrorlines title 'ovs-tas', \
