#!/bin/bash

trap 'killall' INT

killall() {
    trap '' INT TERM     # ignore INT and TERM while shutting down
    echo "**** Shutting down... ****"
    kill -TERM 0
    wait
    echo DONE
}

./my-zmq-pull.minimal-net &
sleep 1
ip -6 address add fdfd::1/64 dev tap0

cat
