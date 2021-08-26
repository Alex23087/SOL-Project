#!/bin/bash

FILEFOLDER="$(dirname "$0")/../test1/files"
TMPFOLDER="$(dirname "$0")/../test1/tmp"
FILE1="$FILEFOLDER/never.txt"
FILE2="$FILEFOLDER/lyrics.txt"

./client -f /tmp/LSOfilestorage.sk -p -t 0 -W $FILE1
sleep 1

./client -f /tmp/LSOfilestorage.sk -p -t 1000 -l $FILE1 -u $FILE1&
./client -f /tmp/LSOfilestorage.sk -p -t 1000 -l $FILE1 -u $FILE1&
./client -f /tmp/LSOfilestorage.sk -p -t 1000 -l $FILE1 -u $FILE1&
./client -f /tmp/LSOfilestorage.sk -p -t 1000 -l $FILE1 -u $FILE1&
./client -f /tmp/LSOfilestorage.sk -p -t 1000 -l $FILE1 -u $FILE1&
./client -f /tmp/LSOfilestorage.sk -p -t 1000 -l $FILE1 -u $FILE1&
./client -f /tmp/LSOfilestorage.sk -p -t 1000 -l $FILE1 -u $FILE1&
./client -f /tmp/LSOfilestorage.sk -p -t 1000 -l $FILE1 -u $FILE1


make hupserver
