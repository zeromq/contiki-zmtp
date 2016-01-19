import time
import zmq

ctx = zmq.Context()
s = ctx.socket(zmq.PUB)
s.setsockopt(zmq.IPV6, 1)

s.connect('tcp://fdfd::ff:fe00:10:9999')
#s.connect('tcp://0:0:0:0:0:0:0:1:9999')

i = 1
while(True):
    msg = 'Hi there! ' + str(i)
    print 'Sending:', msg
    s.send(msg)

    time.sleep(3)
    i += 1
