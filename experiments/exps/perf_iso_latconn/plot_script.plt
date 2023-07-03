#!/usr/bin/gnuplot -persist
set terminal pdf font "Latin Modern Roman"
set output "plot.pdf"
set key autotitle columnhead

set xlabel "Agressor Client # Connections"
set ylabel "Victim Client Avg Latency (µs)"

set yrange[0:]
plot 'lat_50p.dat' using 1:3:7 with yerrorlines title 'virtuoso', \
     'lat_50p.dat' using 1:2:6 with yerrorlines title 'tas', \
     'lat_50p.dat' using 1:4:8 with yerrorlines title 'ovs-linux', \
     'lat_50p.dat' using 1:5:9 with yerrorlines title 'ovs-tas', \


# # margins: left,right,bottom,top
# # spacing: xspacing,yspacing
# set multiplot layout 2,2 \
#               margins 0.1,0.95,0.15,0.90 \
#               spacing 0.1,0.07
              
# set label 1 "Aggresor Client # Connections" at screen 0.5, 0.03 center font "Latin Modern Roman,8"
# set label 2 "Victim Client Latency (µs)" at screen 0.01, 0.5 rotate by 90 center font "Latin Modern Roman,8"

# set title offset 0,-0.6

# # Plot 50p latency
# unset xtics
# set yrange [0:]
# set title "50p Latency"
# set key right bottom
# plot 'lat_50p.dat' using 1:2:4 with yerrorlines title 'bare-virtuoso' linetype 4 ps 0.3, \
#      'lat_50p.dat' using 1:3:5 with yerrorlines title 'virtuoso' linetype 5 ps 0.3, \
#      # 'lat_50p.dat' using 1:5:9 with yerrorlines title 'ovs-linux' linetype 5 ps 0.3, \
#      # 'lat_50p.dat' using 1:6: with yerrorlines title 'ovs-tas' linetype 6 ps 0.3 w lp, \

# # Plot 90p latency
# unset xtics
# set yrange [0:]
# set title "90p Latency"
# set key right bottom
# plot 'lat_90p.dat' using 1:2:4 with yerrorlines title 'bare-virtuoso' linetype 4 ps 0.3, \
#      'lat_90p.dat' using 1:3:5 with yerrorlines title 'virtuoso' linetype 5 ps 0.3, \
#      # 'lat_90p.dat' using 1:5:9 with yerrorlines title 'ovs-linux' linetype 5 ps 0.3, \
#      # 'lat_90p.dat' using 1:6: with yerrorlines title 'ovs-tas' linetype 6 ps 0.3 w lp, \

# # Plot 99p latency
# set xtics
# set xtics font "Latin Modern Roman,4"
# set yrange [0:]
# set title "99p Latency"
# set key right bottom
# plot 'lat_99p.dat' using 1:2:4 with yerrorlines title 'bare-virtuoso' linetype 4 ps 0.3, \
#      'lat_99p.dat' using 1:3:5 with yerrorlines title 'virtuoso' linetype 5 ps 0.3, \
#      # 'lat_99p.dat' using 1:5:9 with yerrorlines title 'ovs-linux' linetype 5 ps 0.3, \
#      # 'lat_99p.dat' using 1:6: with yerrorlines title 'ovs-tas' linetype 6 ps 0.3 w lp, \

# # Plot 99.9p latency
# set xtics
# set xtics font "Latin Modern Roman,4"
# set yrange [0:]
# set title "99.9p Latency"
# set key right bottom
# plot 'lat_99.9p.dat' using 1:2:4 with yerrorlines title 'bare-virtuoso' linetype 4 ps 0.3, \
#      'lat_99.9p.dat' using 1:3:5 with yerrorlines title 'virtuoso' linetype 5 ps 0.3, \
#      # 'lat_99.9p.dat' using 1:5:9 with yerrorlines title 'ovs-linux' linetype 5 ps 0.3, \
#      # 'lat_99.9p.dat' using 1:6: with yerrorlines title 'ovs-tas' linetype 6 ps 0.3 w lp, \
