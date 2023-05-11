#!/bin/bash

if [ -z "$1" ]; then
    echo "Usage: upload.sh file.yaml"
    exit
fi

FILE=$1

if [ -f "$FILE" ]; then
    esphome compile $FILE && while true; do esphome upload $FILE; if [ $? == 0 ]; then break; fi; sleep 10; done
else
    echo "$FILE does not exist."
fi
