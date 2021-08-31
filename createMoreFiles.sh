#!/bin/bash

COPIES=20

for file in ./tests/cats/small/*.c ./tests/cats/small/*.h
do
	i=0
	while [[ $i -le $COPIES ]]
	do
		cp "$file" "$(dirname "$file")/$i$(basename "$file")"&
		i=$((i + 1))
	done
done
