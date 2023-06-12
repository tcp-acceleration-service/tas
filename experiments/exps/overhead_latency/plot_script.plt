#!/usr/bin/gnuplot -persist
set terminal pdf font "Latin Modern Roman"
set output "plot.pdf"
set key autotitle columnhead
set key top left

# Set the colors for each category
set style fill pattern
set style data histograms
set style histogram errorbars
set boxwidth 0.75 relative

set ylabel 'Latency [us]'
set xtics ("tas" 0, "bare-virtuoso" 1, "virtuoso" 2, "tunoff-virtuoso" 3)

plot 'lat.dat' using 2:7 title "50p", \
     'lat.dat' using 3:8 title "90p", \
     'lat.dat' using 4:9 title "99p", \
     'lat.dat' using 5:10 title "99.9p"