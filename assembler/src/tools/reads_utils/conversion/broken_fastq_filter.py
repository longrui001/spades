#!/usr/bin/python

# For filtering C. elegans data set. Some of reads were broken: reads between 103248 and 204343 were replaced by "^@^@^@^...^@^@^@^"

import sys
import os

# MAIN
if len(sys.argv) < 3:
	print("Usage: " + sys.argv[0] + " <input fastq (broken)> <output fastq (filtered)>")	
	sys.exit()

in_file = open(sys.argv[1])
out_file = open(sys.argv[2], 'w')

i = 0
cur_read = []
prev_read = []
is_broken_region = False
for line in in_file:  
    cur_read.append(line)

    if ( (i == 0) and not line.startswith('@') ) or ( (i == 2) and not line.startswith('+') ):
        if not is_broken_region:
            print >> sys.stderr, "Broken started!"
            print >> sys.stderr, "Cur read: ", cur_read
            print >> sys.stderr, "Last correct read: ", prev_read
        is_broken_region = True
        cur_read = []
        i = 0
        continue

    i += 1
    if i == 4:
        is_broken_region = False
        for read_line in cur_read:
            out_file.write(read_line)
        prev_read = cur_read
        cur_read = []
        i = 0

in_file.close()
out_file.close()
        
