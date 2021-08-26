#!/bin/bash

LOGFILE=$1

if [ -z "$LOGFILE" ]
then
	echo "Please provide a log file to parse"
else
	echo "Parsing $LOGFILE"
	TEXT="$(cat "$LOGFILE")"
	TEXT="${TEXT##*Up and running}"


	READS="$(echo "$TEXT" | grep "Sent file to client")"
	READCOUNT="$(echo "$READS" | grep -c .)"
	READAVG=0

	if [[ "$READCOUNT" -ne 0 ]]
	then
		IFS=$'\n'
		for line in ${READS//\\n/$cr}
		do
			CURRENTREAD="$(echo "$line" | grep -Eo "[0-9]* bytes" | grep -Eo "[0-9]*")"
			READAVG=$((READAVG + CURRENTREAD))
		done

		READAVG=$((READAVG / READCOUNT))
	fi

	WRITES="$(echo "$TEXT" | grep "Received file from client")"
	WRITECOUNT="$(echo "$WRITES" | grep -c .)"
	WRITEAVG=0

	if [[ "$WRITECOUNT" -ne 0 ]]
	then
		IFS=$'\n'
		for line in ${WRITES//\\n/$cr}
		do
			CURRENTWRITE="$(echo "$line" | grep -Eo "[0-9]* bytes" | grep -Eo "[0-9]*")"
			WRITEAVG=$((WRITEAVG + CURRENTWRITE))
		done

		WRITEAVG=$((WRITEAVG / WRITECOUNT))
	fi


	echo "Number of reads: $READCOUNT, average size: $READAVG bytes"
	echo "Number of writes: $WRITECOUNT, average size: $WRITEAVG bytes"
	echo "Number of locks acquired: $(echo "$TEXT"  | grep -c "successfully locked")"
	echo "Number of open-lock operations: $(($(echo "$TEXT" | grep -c "flags: 3") + $(echo "$TEXT" | grep -c "flags: 2")))"
	echo "Number of locks released: $(echo "$TEXT" | grep -c "successfully unlocked")"
	echo "Number of file close operations: $(echo "$TEXT" | grep -c "successfully closed the file")"
	echo "Max size reached: $(echo "scale=4; $(echo "$TEXT" | grep "Max storage size" | grep -Eo "[0-9]*")/1024/1024" | bc -l)MB"
	echo "Max files stored: $(echo "$TEXT" | grep "Max number of files stored" | grep -Eo "[0-9]*")"
	echo "Num files evicted: $(echo "$TEXT" | grep "Number of files evicted" | grep -Eo "[0-9]*")"
	echo "$TEXT" | grep -Eo "Worker #[0-9]* has served [0-9]* requests"
	echo "Max clients connected: $(echo "$TEXT" | grep "Max number of clients simultaneously connected" | grep -Eo "[0-9]*")"
fi
