#! /bin/bash

sudo ./app/app/x86_64-native-linuxapp-gcc/pktgen -c 0x1f -n 3 -- -P -m "[1:3].0, [2:4].1"
