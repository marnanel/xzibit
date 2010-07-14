#!/usr/bin/python
# Script for testing xzibit-bus-server.

import sys
import socket
import os

def dump(s):
    result = ''
    for char in s:
        result += ' %02x' % (ord(char),)
    return result[1:]

filename = '/tmp/xzibit-bus'

server = socket.socket(socket.AF_UNIX,
                       socket.SOCK_STREAM)

server.connect(filename)

print 'Client ',os.getpid(),' is connected.'

if len(sys.argv)>1 and sys.argv[1]=='-s':

    port = 7177

    message = chr(1) + chr(127)+chr(0)+chr(0)+chr(1) + chr(port&0xFF)+chr((port>>8)&0xFF)

    length = ''
    temp = len(message)
    for bytes in range(4):
        length += chr(temp & 0xFF)
        temp >>= 8
    print 'Client ',os.getpid(),' sends ',dump(message)
    server.send(length+message)

print 'Client ',os.getpid(),' receives ',dump(server.recv(1024)), 'and quits'

server.close()
