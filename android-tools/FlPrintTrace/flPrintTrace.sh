#!/bin/bash
#check if has device
adb devices

#get process name and pid
processName=$1;
echo $processName;
adb root
processInfo=$(adb shell ps |grep $processName);
echo $processInfo;
processID=`echo $processInfo | awk  '{print $2}'`;  
echo $processID; 
#generate the trace file and pull to current direct
#adb shell rm -r /data/anr/traces.txt
adb shell kill -s 3 $processID
adb pull /data/anr/traces.txt .

