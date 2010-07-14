#!/usr/bin/python

# This is a VERY simple bus server for xzibit so that we can
# experiment using two clients on the same host without running
# two DBus and Telepathy instances.
#
# This is not what will be used in the final version.
# In that brave day, we will use Telepathy tubes.
#
# There will be a Unix domain socket at "/tmp/xzibit-bus".
# If a client sends <length>+<message>, where <message> is
# any amount of data, and <length> is the length of <message>
# expressed in four little-endian bytes, then <length>+<message>
# will be transmitted to all the other clients.
#
# The first byte of <message> is intended to be an opcode.
# This server contains code to interpret <message> according
# to the value of <opcode> in some cases.

import socket
import select
import os
import os.path
import sys

def dump(s):
    "Attempts to print information about data flowing over the bus"

    message_type = ord(s[0])

    if message_type==1:
        print 'new window on %d.%d.%d.%d : %d' % (
            ord(s[1]),
            ord(s[2]),
            ord(s[3]),
            ord(s[4]),
            ord(s[6])*256+ord(s[5]))
    else:
        print 'unknown message type: %d' % (message_type,)

buffer = {}

filename = '/tmp/xzibit-bus'

if os.path.exists(filename):
    # There can be only one.
    os.unlink(filename)

server = socket.socket(socket.AF_UNIX,
                       socket.SOCK_STREAM)

server.bind(filename)

waiting = [server]

server.listen(1)

while 1:
    ready = select.select(waiting, [], [])[0]

    for sock in ready:
        if sock==server:
            (connection, addr) = server.accept()
            print 'client connected'
            waiting.append(connection)
        else:
            got = sock.recv(1024)
            if len(got)==0:
                # assume eof
                print 'client disconnected'
                waiting.remove(sock)
            else:
                buffer[sock.fileno()] = buffer.get(sock.fileno(), '') + got

    # see whether anything's ready to send

    for fd in buffer.keys():
        if len(buffer[fd])<4:
            continue

        needed = 0
        for i in range(3, -1, -1):
            needed <<= 8
            needed |= ord(buffer[fd][i])

        needed += 4 # plus header

        if len(buffer[fd]) >= needed:
            # we have a full record
            dump(buffer[fd][4:needed])
            for remote in waiting:
                if remote==server or remote.fileno()==fd:
                    # don't send to the server socket,
                    # nor to the originating socket
                    continue
                remote.send(buffer[fd][0:needed])
            buffer[fd] = buffer[fd][needed:]
