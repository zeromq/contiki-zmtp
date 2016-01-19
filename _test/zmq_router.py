import zmq

ctx = zmq.Context()
s = ctx.socket(zmq.ROUTER)
s.setsockopt(zmq.IPV6, 1)

#s.bind('tcp://*:9999')
s.connect('tcp://fdfd::ff:fe00:10:9999')

while(True):
    r = s.recv_multipart()
    print '< ', r[1:]
    s.send(r[0], flags=zmq.SNDMORE)
    s.send_multipart(raw_input('> ').split(' '))
