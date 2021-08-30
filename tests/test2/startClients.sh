#!/bin/bash

FILEFOLDER="$(dirname "$0")/../cats"
SMALLFILEFOLDER="$FILEFOLDER/small"
MEDIUMFILEFOLDER="$FILEFOLDER/medium"
BIGFILEFOLDER="$FILEFOLDER/big"
TMPFOLDER="$(dirname "$0")/tmp"

for file in "$SMALLFILEFOLDER"/* "$MEDIUMFILEFOLDER"/*
do
	./client -f /tmp/LSOfilestorage.sk -p -t 200 -D "$TMPFOLDER" -W "$file" &
	sleep 0.2
done

make hupserver
