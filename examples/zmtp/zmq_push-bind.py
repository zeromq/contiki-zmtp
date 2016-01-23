import time
import zmq

ctx = zmq.Context()
s = ctx.socket(zmq.PUSH)
s.setsockopt(zmq.IPV6, 1)

s.bind('tcp://*:9999')

i = 1
while(True):
    msg = 'Hi there! ' + str(i)
    print 'Sending:', msg
    s.send(msg)

    time.sleep(3)
    i += 1
