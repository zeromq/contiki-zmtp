import zmq

ctx = zmq.Context()
s = ctx.socket(zmq.PULL)
s.setsockopt(zmq.IPV6, 1)

s.connect('tcp://fdfd::ff:fe00:10:9999')
#s.connect('tcp://0:0:0:0:0:0:0:1:9999')

while(True):
    print '< ', s.recv_multipart()
