#!/bin/sh
# Solve the push/fold grids the bot uses (about 1.5 h on 4 cores).  Run from the repo root.
set -e
B=build/pfn/pfn
$B grid 3 3 2.5,3.5,5,7,10,14,20 data/pfn/grid_3p3.bin 300
$B grid 3 4 2.5,3.5,5,7,10,14,20 data/pfn/grid_3p4.bin 300
$B grid 4 4 2.5,4,6,9,13,20 data/pfn/grid_4p4.bin 200
