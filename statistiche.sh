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
	READCOUNT="$(echo "$READS" | wc -l)"
	READAVG=0

	if [[ "$READCOUNT" -ne 0 ]]
	then
		IFS=$'\n'
		for line in ${READS//\\n/$cr}
		do
			CURRENTREAD="$(echo "$line" | grep -Eo "[0-9]* bytes" | grep -Eo "[0-9]*")"
			READAVG=$(( "$READAVG" + "$CURRENTREAD"))
		done

		READAVG=$(( "$READAVG" / "$READCOUNT" ))
	fi

	WRITES="$(echo "$TEXT" | grep "Received file from client")"
	WRITECOUNT="$(echo "$WRITES" | wc -l)"
	WRITEAVG=0

	if [[ "$WRITECOUNT" -ne 0 ]]
	then
		IFS=$'\n'
		for line in ${WRITES//\\n/$cr}
		do
			CURRENTWRITE="$(echo "$line" | grep -Eo "[0-9]* bytes" | grep -Eo "[0-9]*")"
			WRITEAVG=$(( "$WRITEAVG" + "$CURRENTWRITE"))
		done

		WRITEAVG=$(( "$WRITEAVG" / "$WRITECOUNT" ))
	fi

	echo "Number of reads: $READCOUNT, average size: $READAVG bytes"
	echo "Number of writes: $WRITECOUNT, average size: $WRITEAVG bytes"
	echo "Number of locks acquired: $(echo "$TEXT"  | grep -Ec "successfully locked")"
	echo "Number of open-lock operations: $(($(echo "$TEXT" | grep -Ec "flags: 3") + $(echo "$TEXT" | grep -Ec "flags: 2")))"
	echo "Number of locks released: $(echo "$TEXT" | grep -Ec "successfully unlocked")"
fi