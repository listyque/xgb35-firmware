cat /dev/kmsg > /tmp/kmsg.txt & P=$!; sleep 3; kill $P
grep -E 'blk dev|MiscInfo|Head failed|hdcp|t:7|umon|Booted|wakeup' /tmp/kmsg.txt | tail -12
