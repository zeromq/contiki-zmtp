import zmq

ctx = zmq.Context()
s = ctx.socket(zmq.DEALER)
s.setsockopt(zmq.IPV6, 1)

s.connect('tcp://fdfd::ff:fe00:10:9999')
#s.connect('tcp://0:0:0:0:0:0:0:1:9999')

print 'Sending...'
s.send_multipart(['Hello', 'World'])

while(True):
    print '< ', s.recv_multipart()
    s.send_multipart(raw_input('> ').split(' '))
