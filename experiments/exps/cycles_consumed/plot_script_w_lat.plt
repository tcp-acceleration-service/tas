#!/usr/bin/gnuplot -persist
set terminal pdf
set output "plot.pdf"
set colorsequence podo
set key autotitle columnhead

set autoscale noextend

mleft=0.1
mright=0.98
mbottom=0.1
mtop=0.90
xspacing=0.1
yspacing=0.1

xgraphsize = (1 - mleft - (1 - mright) - xspacing) / 2
ygraphsize = (1 - mbottom - (1 - mtop) - yspacing) / 2

set multiplot layout 2,2 \
              margins mleft,mright,mbottom,mtop \
              spacing xspacing,yspacing \
              title "vm0: 512 conns 3 cores vm1: 4096 conns 3 cores" font "Computer Modern Roman,10"

set title offset 0,-0.6

stats 'client0.dat' using 1 nooutput name 'C0TS_'
stats 'client1.dat' using 1 nooutput name 'C1TS_'
stats 'cycles.dat' using 1 nooutput name 'TASTS_'

set title font "Computer Modern Roman,6"
set key font "Computer Modern Roman,6"
set label font "Computer Modern Roman,8"
set ytics font "Computer Modern Roman,6"
set xtics font "Computer Modern Roman,6"

# Cycles Consumed by Phase Bar Chart
set yrange [0:]
set style data histogram
set style histogram cluster gap 1
set style fill solid
set boxwidth 0.9
set grid ytics
set key left top
plot "cycles_phase_agg.dat" using 3:xtic(1) title "poll" linetype 2, \
     "cycles_phase_agg.dat" using 4 title "tx" linetype 3, \
     "cycles_phase_agg.dat" using 5 title "rx" linetype 4, \
     "cycles_phase_agg.dat" using 2 title "total" linetype 6


unset xtics

# Plot aggregate number of messages
set yrange [0:]
set style data histogram
set style histogram cluster gap 1
set style fill solid
set boxwidth 0.9
set grid ytics
set key left top
plot "client0_lat.dat" using 1 title "50p" linetype 2, \
     "client0_lat.dat" using 2 title "90p" linetype 3, \
     "client0_lat.dat" using 3 title "99p" linetype 4, \
     "client0_lat.dat" using 4 title "99.9p" linetype 6, \
     "client0_lat.dat" using 5 title "99.99p" linetype 7


# Plot cycles per second
set yrange [0:]
set arrow from C0TS_min, graph 0 to C0TS_min, graph 1 nohead linetype 2 dashtype 3 lw 2
set arrow from C1TS_min, graph 0 to C1TS_min, graph 1 nohead linetype 3 dashtype 3 lw 2
set key right top
set label 3 "Time" at screen mleft + (xgraphsize / 2), mbottom / 2 center \
     font "Computer Modern Roman,6"
set label 4 "Cycles per second" at screen (mleft / 4), \
     mbottom + (ygraphsize / 2) \
     rotate by 90 center font "Computer Modern Roman,6"
plot 'cycles.dat' using 1:2 title 'vm0' linetype 2 w l, \
     'cycles.dat' using 1:7 title 'vm1' linetype 3 w l, \
     0,0 title 'app0 start' linetype 2 dashtype 3 w l, \
     0,0 title 'app1 start' linetype 6 dashtype 3 w l \

unset arrow


# Plot throughput per second
set yrange [0:2200]
set xrange [TASTS_min: TASTS_max]
set key right bottom
set label 5 "Time" at screen mright - (xgraphsize / 2), mbottom / 2 center \
     font "Computer Modern Roman,6"
set label 6 "Throughput (Mbps)" at screen mleft + xgraphsize + (xspacing / 4), \
     mbottom + (ygraphsize / 2) \
     rotate by 90 center font "Computer Modern Roman,6"
plot 'client0.dat' using 1:3 title 'vm0' linetype 2 w l, \
     'client1.dat' using 1:3 title 'vm1' linetype 3 w l \
