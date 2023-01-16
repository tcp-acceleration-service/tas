#!/usr/bin/gnuplot -persist
set terminal pdf
set output "test_activeon.pdf"
set colorsequence podo
set key autotitle columnhead
unset xtics
set autoscale noextend

set title font "Computer Modern Roman,6"
set key font "Computer Modern Roman,6"
set label font "Computer Modern Roman,8"
set ytics font "Computer Modern Roman,6"

mleft=0.1
mright=0.98
mbottom=0.1
mtop=0.90
xspacing=0.1
yspacing=0.06

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

# Plot total cycles consumed
set yrange [0:]
set arrow from C0TS_min, graph 0 to C0TS_min, graph 1 nohead linetype 2 dashtype 3 lw 2
set arrow from C1TS_min, graph 0 to C1TS_min, graph 1 nohead linetype 3 dashtype 3 lw 2
set label 1 "Total cycles consumed" at screen (mleft / 4), \
     1 - (1 - mtop) - (ygraphsize / 2) \
     rotate by 90 center font "Computer Modern Roman,6"
set key center top
plot 'cycles.dat' using 1:3 title 'vm0' linetype 2 w l, \
     'cycles.dat' using 1:5 title 'vm1' linetype 3 w l, \
     0,0 title 'app0 start' linetype 2 dashtype 3 w l, \
     0,0 title 'app1 start' linetype 3 dashtype 3 w l \

unset arrow


# Plot aggregate number of messages
set yrange [0:]
set xrange [TASTS_min: TASTS_max]
set label 2 "Total # of messages" at screen mleft + xgraphsize + (xspacing / 4), \
     1 - (1 - mtop) - (ygraphsize / 2) \
     rotate by 90 center font "Computer Modern Roman,6"
set key left top
plot 'client0.dat' using 1:2 title 'vm0' linetype 2 w l, \
     'client1.dat' using 1:2 title 'vm1' linetype 3 w l \


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
     'cycles.dat' using 1:4 title 'vm1' linetype 3 w l, \
     0,0 title 'app0 start' linetype 2 dashtype 3 w l, \
     0,0 title 'app1 start' linetype 3 dashtype 3 w l \

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