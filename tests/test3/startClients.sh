#!/bin/bash

FILEFOLDER="$(dirname "$0")/../cats"
SMALLFILEFOLDER="/mnt/e/Progetti/SOL-Project/Screenshots"
SMALLFILEFOLDER="$FILEFOLDER/small"
MEDIUMFILEFOLDER="$FILEFOLDER/medium"
BIGFILEFOLDER="$FILEFOLDER/big"
TMPFOLDER="$(dirname "$0")/tmp"

CLIENTNUM=10
CURRENTCLIENTS=0

files=("$SMALLFILEFOLDER"/*)
RANDOM=$$$(date +%s)


while true
do
	if [[ CURRENTCLIENTS -lt CLIENTNUM ]]
	then
		FILE1="${files[$RANDOM % ${#files[@]}]}"
		FILE2="${files[$RANDOM % ${#files[@]}]}"
		./client -f /tmp/LSOfilestorage.sk -t 0 -D "$TMPFOLDER" -d "$TMPFOLDER" -W "$FILE1" -r "$FILE1" -c "$FILE1" -W "$FILE2" -c "$FILE2"&
		CURRENTCLIENTS=$((CURRENTCLIENTS + 1))
	else
		wait -n
		CURRENTCLIENTS=$((CURRENTCLIENTS - 1))
	fi
done&

sleep 30
kill $! &
make intserver
exit