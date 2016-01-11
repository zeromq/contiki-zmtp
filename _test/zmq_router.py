import zmq

ctx = zmq.Context()
s = ctx.socket(zmq.ROUTER)
s.setsockopt(zmq.IPV6, 1)

s.bind('tcp://*:9999')

while(1):
    print s.recv()
