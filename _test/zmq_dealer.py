import zmq

ctx = zmq.Context()
s = ctx.socket(zmq.DEALER)
s.setsockopt(zmq.IPV6, 1)

s.connect('tcp://fdfd::ff:fe00:10:9999')
#s.connect('tcp://aaaa::900:f4ff:7439:8bc6:9999')
#s.connect('tcp://0:0:0:0:0:0:0:1:9999')

print 'Sending...'
s.send_multipart(['Hello', 'World'])
