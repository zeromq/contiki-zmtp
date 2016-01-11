import zmq

ctx = zmq.Context()
s = ctx.socket(zmq.STREAM)
s.setsockopt(zmq.IPV6, 1)

s.connect('tcp://aaaa::900:f4ff:7439:8bc6:80')
#s.connect('tcp://0:0:0:0:0:0:0:1:80')

id_ = s.getsockopt(zmq.IDENTITY)
print 'Found identity', id_

to_send = 'GET /3\n'
print 'Sending', to_send
s.send_multipart([id_, to_send])


print 'Receiving... ', s.recv_multipart()
