#!/bin/bash

FILEFOLDER="$(dirname "$0")/files"
TMPFOLDER="$(dirname "$0")/tmp"
FILE1="$FILEFOLDER/never.txt"
FILE2="$FILEFOLDER/lyrics.txt"

./client -f /tmp/LSOfilestorage.sk -p -t 200 -d $TMPFOLDER -D $TMPFOLDER -W $FILE1 -a $FILE1,$FILE2 -R 1 -l $FILE1 -u $FILE1 -c $FILE1 -w $FILEFOLDER,n=0 -r $FILE2&&

make hupserver
