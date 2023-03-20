#!/usr/bin/gnuplot -persist
set terminal pdf
set output "plot.pdf"
set colorsequence podo
set key autotitle columnhead
set bmargin 3
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
              title "vm0: 64B 512 conns 3 cores vm1: 64B 4096 conns 3 cores" font "Computer Modern Roman,10"

set title offset 0,-0.6

stats 'client0.dat' using 1 nooutput name 'C0TS_'
stats 'client1.dat' using 1 nooutput name 'C1TS_'
stats 'cycles.dat' using 1 nooutput name 'TASTS_'

set title font "Computer Modern Roman,6"
set key font "Computer Modern Roman,6"
set label font "Computer Modern Roman,8"
set ytics font "Computer Modern Roman,6"
set xtics font "Computer Modern Roman,6"

unset xtics

# Plot Cycles Consumed by Phase Bar Chart
set key right top
set yrange [0:1]
set label 1 "Time" at screen mleft + (xgraphsize / 2), mbottom / 2 center \
     font "Computer Modern Roman,6"
set label 2 "Cycles (phase / total)" at screen (mleft / 4), \
     1 - (1 - mtop) - (ygraphsize / 2) \
     rotate by 90 center font "Computer Modern Roman,6"
plot 'phases.dat' using ($1):($3 / $2) title 'vm0-poll' linetype rgb "#d1e5f0" ps 0.3 w lp, \
     'phases.dat' using ($1):($4 / $2) title 'vm0-tx' linetype rgb "#67a9cf" ps 0.3 w lp, \
     'phases.dat' using ($1):($5 / $2) title 'vm0-rx' linetype rgb "#2166ac" ps 0.3 w lp, \
     'phases.dat' using ($1):($6 / $2) title 'vm1-poll' linetype rgb "#fddbc7" ps 0.3 w lp, \
     'phases.dat' using ($1):($7 / $2) title 'vm1-tx' linetype rgb "#ef8a62" ps 0.3 w lp, \
     'phases.dat' using ($1):($8 / $2) title 'vm1-rx' linetype rgb "#b2182b" ps 0.3 w lp, \

unset arrow


# Plot latency
# set yrange [0:]
# set style data histogram
# set style histogram cluster gap 1
# set style fill solid
# set boxwidth 0.9
# set grid ytics
# set label 2 "Latency (μs)" at screen mleft + xgraphsize + (xspacing / 4), \
#      1 - (1 - mtop) - (ygraphsize / 2) \
#      rotate by 90 center font "Computer Modern Roman,6"
# set key left top
# plot "client0_lat.dat" using 1 title "50p" linetype 2, \
#      "client0_lat.dat" using 2 title "90p" linetype 3, \
#      "client0_lat.dat" using 3 title "99p" linetype 4, \
#      "client0_lat.dat" using 4 title "99.9p" linetype 6, \
#      "client0_lat.dat" using 5 title "99.99p" linetype 7
# Plot budget
set arrow from C0TS_min, graph 0 to C0TS_min, graph 1 nohead linetype rgb "#2166ac" dashtype 3 lw 2
set arrow from C1TS_min, graph 0 to C1TS_min, graph 1 nohead linetype rgb "#b2182b" dashtype 3 lw 2
set key right top
set yrange [0:1]
set label 3 "Time" at screen mleft + (xgraphsize / 2), mbottom / 2 center \
     font "Computer Modern Roman,6"
set label 4 "Budget" at screen mleft + xgraphsize + (xspacing / 4), \
     1 - (1 - mtop) - (ygraphsize / 2) \
     rotate by 90 center font "Computer Modern Roman,6"
plot 'cycles.dat' using ($1):($4 / 2100000.0) title 'vm0' linetype rgb "#2166ac" ps 0.3 w lp, \
     'cycles.dat' using ($1):($6 / 2100000.0) title 'vm1' linetype rgb "#b2182b" ps 0.3 w lp, \
     0,0 title 'app0 start' linetype rgb "#2166ac" dashtype 3 w l, \
     0,0 title 'app1 start' linetype rgb "#b2182b" dashtype 3 w l \

unset arrow


# Plot cycles per second
set arrow from C0TS_min, graph 0 to C0TS_min, graph 1 nohead linetype rgb "#2166ac" dashtype 3 lw 2
set arrow from C1TS_min, graph 0 to C1TS_min, graph 1 nohead linetype rgb "#b2182b" dashtype 3 lw 2
set key right top
set yrange [0:1]
set label 5 "Time" at screen mleft + (xgraphsize / 2), mbottom / 2 center \
     font "Computer Modern Roman,6"
set label 6 "Cycles (phases sum / total)" at screen (mleft / 4), \
     mbottom + (ygraphsize / 2) \
     rotate by 90 center font "Computer Modern Roman,6"
plot 'cycles.dat' using ($1):($3 / $2) title 'vm0' linetype rgb "#2166ac" ps 0.3 w lp, \
     'cycles.dat' using ($1):($5 / $2) title 'vm1' linetype rgb "#b2182b" ps 0.3 w lp, \
     0,0 title 'app0 start' linetype rgb "#2166ac" dashtype 3 w l, \
     0,0 title 'app1 start' linetype rgb "#b2182b" dashtype 3 w l \

unset arrow


# Plot throughput per second
set xrange [TASTS_min: TASTS_max]
set key right center
set yrange[0:3000]
set label 7 "Time" at screen mright - (xgraphsize / 2), mbottom / 2 center \
     font "Computer Modern Roman,6"
set label 8 "Throughput (Mbps)" at screen mleft + xgraphsize + (xspacing / 4), \
     mbottom + (ygraphsize / 2) \
     rotate by 90 center font "Computer Modern Roman,6"
plot 'client0.dat' using 1:3 title 'vm0' linetype rgb "#2166ac" ps 0.3 w lp, \
     'client1.dat' using 1:3 title 'vm1' linetype rgb "#b2182b" ps 0.3 w lp, \