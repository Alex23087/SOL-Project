#!/bin/bash

LOGFILE=$1

if [ -z "$LOGFILE" ]
then
	echo "No logfile path provided. Assuming default location"
	LOGFILE='/tmp/LSOfilestorage.log'
fi
echo "Parsing $LOGFILE"

if [ ! -f "$LOGFILE" ]; then
    echo "File '$LOGFILE' does not exist."
    exit
fi
TEXT="$(tac "$LOGFILE" | sed '/Up and running/Q' | tac)"
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
echo "Number of locks explicitly acquired (locks obtained with open-lock not counted): $(echo "$TEXT"  | grep -c "successfully locked")"
echo "Number of open-lock operations: $(($(echo "$TEXT" | grep -c "successfully opened and locked") + $(echo "$TEXT" | grep -c "flags: 2")))"
echo "Number of locks explicitly released (locks released with close/remove not counted): $(echo "$TEXT" | grep -c "successfully unlocked")"
echo "Number of file close operations: $(echo "$TEXT" | grep -c "successfully closed the file")"
echo "Max size reached: $(echo "scale=4; $(echo "$TEXT" | grep -Eo "Max storage size.*" | grep -Eo "[0-9]*")/1024/1024" | bc -l)MB"
echo "Max files stored: $(echo "$TEXT" | grep -Eo "Max number of files stored.*" | grep -Eo "[0-9]*")"
echo "Num files evicted: $(echo "$TEXT" | grep -Eo "Number of files evicted.*" | grep -Eo "[0-9]*")"
echo "$TEXT" | grep -Eo "Worker #[0-9]* has served [0-9]* requests"
echo "Max clients connected: $(echo "$TEXT" | grep -Eo "Max number of clients simultaneously connected.*" | grep -Eo "[0-9]*")"
echo "Number of files compressed: $(echo "$TEXT" | grep -c "File has been compressed")"

SIZECOMPRESSED=0
SIZEUNCOMPRESSED=0

STORES="$(echo "$TEXT" | grep "File has been compressed")"

IFS=$'\n'
for line in ${STORES//\\n/$cr}
do
	CURRENTSTORE="$(echo "$line" | grep -Eo "[0-9]* bytes" | grep -Eo "[0-9]*")"

	CURRENTUNCOMPRESSED=$(echo $CURRENTSTORE | cut -d " " -f 1)
	SIZEUNCOMPRESSED=$((SIZEUNCOMPRESSED + CURRENTUNCOMPRESSED))

	CURRENTCOMPRESSED=$(echo $CURRENTSTORE | cut -d " " -f 2)
	SIZECOMPRESSED=$((SIZECOMPRESSED + CURRENTCOMPRESSED))
done


STORES="$(echo "$TEXT" | grep "File not compressed")"

IFS=$'\n'
for line in ${STORES//\\n/$cr}
do
	CURRENTSTORE="$(echo "$line" | grep -Eo "[0-9]* bytes" | grep -Eo "[0-9]*")"
	SIZEUNCOMPRESSED=$((SIZEUNCOMPRESSED + CURRENTSTORE))
	SIZECOMPRESSED=$((SIZECOMPRESSED + CURRENTSTORE))
done


echo "Size compressed $SIZECOMPRESSED bytes, size uncompressed: $SIZEUNCOMPRESSED bytes, bytes saved: $((SIZEUNCOMPRESSED - SIZECOMPRESSED))"
echo "Compression ratio: $(echo "scale=4; $SIZECOMPRESSED / $SIZEUNCOMPRESSED * 100" | bc -l)%"
