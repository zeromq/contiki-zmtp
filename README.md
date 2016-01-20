ZMTP for Contiki OS
===================

This is a partial port of the ZeroMQ Message Transfer Protocol [ZMTP](http://zeromq.org/) for [Contiki OS](http://www.contiki-os.org/). It is based on the [libzmtp](https://github.com/zeromq/libzmtp), tiny ZMTP library in C.

Typical target is an ARM Cortext M3 running at 32MHz, with 512KB of flash, and 80KB of RAM. And with a sub-1GHz RF link. The implementation makes use of Contiki network stack, which can use TCP, IPv4, IPv6 and 6LoWPAN. The demo program is about 130KB and runs over 6LoWPAN on the sub-GHz link.

Supported features
------------------

* Implements [ZMTP 3.0](http://rfc.zeromq.org/spec:23)
* Implements following sockets:
  * PUB/SUB
  * PUSH/PULL
  * ROUTER/DEALER

Limitations
-----------

It was not possible to implement every thing according to the RFCs. Here is a list of known limitations.

- Due to Contiki's protothread, it blocks processing of incoming data and other outgoing data when sending to a peer
- Due to Contiki's protothread, router blocks on sending (at least for a part of the sending process)
- Due to Contiki's protothread and the necessity to use static variables in functions to keep the state of a function's local variables, creating several sockets of the same type and reading/sending from them at the same time can potentially lead to undefined behaviours.
- Whenever a remote peer we connected to disconnect, its in/out queues are destroyed (how to trace who is who with Contiki's net stack?)
- No runtime configurable HWM (how should it count? Per connection or per socket?), but implementation is partially ready for it ATM
- Fair queuing is implemented just as a loop cycling through all connections
- If received data is bigger than ZMTP_INPUT_BUFFER_SIZE (default: 200), it will not be able to read it over several buffer reads,
  consider it as an error and thus will close the connection
- Same for output data bigger than ZMTP_OUTPUT_BUFFER_SIZE (default: 200), it will have an undefined behaviour
- There is nothing to close/stop a socket
- Can only bind, but connecting should not be hard to implement (just need to parse address strings)
- Only one PUB socket can be created
- SUB sockets can only subscribe (but PUB do support unsubcriptions), but subscriptions will only be sent once to currently connected peer

Known to work on
----------------

This implementation has been tested with the following Contiki's target:
- minimal-net (ie. locally on your host)
- stm32nucleo-spirit1 (ARM Cortext M3 STM32L152RE with Spirit1 868MHz RF module)

Trying it on a local host (Linux)
---------------------------------

```
$ git clone --recursive https://github.com/Alidron/contiki-zeromq.git
$ cd contiki-zeromq
$ make TARGET=minimal-net
$ sudo ./start-pub.sh
```

That last command started a publisher example (cf. my-zmq-pub.c) on your computer. Its network interface is mapped to the tap0 host's interface.

Sudo is needed here to let the program create the tap0 interface, as well as to create a mapping of IPv6 addresses fdfd::1/64 to this interface.

The "remote" host is accessible at the address fdfd::ff:fe00:10 (hardcoded in the Makefile for the minimal-net target).

The PUB socket listen on port 9999. You can try a simple Python script to subscribe to what the publisher says (you need pyzmq installed):
```
$ cd _test
$ python zmq_sub.py
```

Trying it on some real constrained hardware
-------------------------------------------

The following demo shows how to use the publisher example program on real constrained hardware, over an RF link, 6LoWPAN, TCP and ZeroMQ as transports.

In this case we will do with the stm32nucleo-spirit1 platform. But any other platform supported by Contiki should work as well.

The MCU is an ARM Cortext M3 STM32L152RE, 32MHz, 512KB of Flash and 80KB of RAM. The RF module is a sub-1GHz Spirit1 module from ST. This setup can be made of development kits: NUCLEO-L152RE + X-NUCLEO-IDS01A4, for about $25 per node. You will need at least two nodes (one running the publisher example program, and another one running the border router). The setup is actually described in [this evaluation setup](http://www.st.com/web/en/catalog/tools/PF263051) from ST.

### 1. Setup the border router

This part is described in this [Getting started guide (UM2000)](http://www.st.com/st-web-ui/static/active/en/resource/technical/document/user_manual/DM00255309.pdf) from ST. The important parts are reproduced here:
```
$ cd contiki/examples/ipv6/rpl-border-router
$ make TARGET=stm32nucleo-spirit1
$ arm-none-eabi-objcopy -O binary border-router.stm32nucleo-spirit1 br.bin
```
Then connect your NUCLEO-L152RE node with USB, and copy the br.bin file to the removable disk to flash the MCU.

Then instantiate an IPv6 packet bridge on your local host:
```
$ cd ../../../tools
$ make tunslip6
$ sudo ./tunslip6 -s /dev/ttyACM0 aaaa::1/64
```
And press the reset (black) button of the NUCLEO board to have it recognized by the packet bridge.

Note the first address (starting with 'aaaa') in the output of tunslip6. Eg. `aaaa::800:f5ff:eb3a:14c5`. This is the address of the border router. We will need it later.

### 2. Setup the publisher example program

```
$ cd ../.. # Back to this repo root
$ make TARGET=stm32nucleo-spirit1
$ arm-none-eabi-objcopy -O binary my-zmq-pub.stm32nucleo-spirit1 my-zmq-pub.bin
```
Then connect your other NUCLEO-L152RE node with USB, and copy the my-zmq-pub.bin file to the removable disk to flash the MCU.

You can see what the program is outputting on its UART with minicom:
```
$ minicom -b 115200 -D /dev/ttyACM1
```

For the sake of being sure there is no monkey business ( :) ), you can connect that second node to another computer running on a separated network. Just keep the RF modules in reach (~100m in open air?).

### 3. Start a SUB client

You need to know what is the IPv6 address of the remote node. For this, open a web browser to the address of the border router you previously noted. Don't forget to add square brackets around the address in the URL bar to have your browser recognize it as an IPv6 address. Eg. `[aaaa::800:f5ff:eb3a:14c5]`.

The page should list you neighbors and routes. We are interested into the address of the remote node. Pick the one in the routes list that start with an 'aaaa' prefix. Eg. `aaaa::a00:f7ff:b9bc:4643`.

You can try to ping it (hint: do a reset of the node while pinging to make yourself sure it is the right node ;)):
```
$ ping6 aaaa::a00:f7ff:b9bc:4643
```

Now, edit the file _test/zmq_sub.py and replace in the connect line with your own IPv6 address. For instance:
```
...
s.connect('tcp://aaaa::a00:f7ff:b9bc:4643:9999')
...
```

Now run this Python script:
```
$ cd _test
$ python zmq_sub.py
```

After waiting a couple of seconds (yes, it is a bit slugish) you should see whatever the publisher node spit out :).

TODO
----

* Implement runtime configurable in/out queues (ie. High Water Mark)
* Make it able to deal with very large packets (ie. bigger than one input or output TCP buffers)
* Automated tests!
* Fix all known limitations mentioned above

License
-------

MPLv2
