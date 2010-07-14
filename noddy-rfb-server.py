# Noddy VNC server for testing.
# Photo of Xzibit (c) Sjoerd ten Kate from London, licensed under cc-by-sa.

import socket
import sys
import gd
import random

host = None
port = 50008
s = None

def read_n(connection, bytes):
    result = 0
    for i in range(0, bytes):
        result <<= 8
        result |= ord(connection.recv(1))
    return result

def read8(connection):
    return ord(connection.recv(1))

def read16(connection):
    return read_n(connection, 2)

def read32(connection):
    return read_n(connection, 4)

def send_n(connection, data, bytes):
    shift = (bytes-1) * 8
    for i in range(0, bytes):
        connection.send(chr( (data>>shift)&0xFF ))
        shift -= 8

def send8(connection, data):
    connection.send(chr(data & 0xFF))

def send16(connection, data):
    send_n(connection, data, 2)

def send32(connection, data):
    send_n(connection, data, 4)

def send_image(connection, image):
    send8(connection, 0) # FramebufferUpdate
    send8(connection, 0) # padding
    send16(connection, 1) # number of rectangles

    # now our sole rectangle
    send16(connection, 0) # x
    send16(connection, 0) # y
    (width, height) = image.size()
    send16(connection, width)
    send16(connection, height)
    send32(connection, 0) # encoding type is "raw"
    pixels = ''
    for y in range(0, height):
        for x in range(0, width):
            pixel = image.getPixel( (x, y) )
            pixels += chr(0) + \
                chr(image.red(pixel)) + \
                chr(image.green(pixel)) + \
                chr(image.blue(pixel))
    connection.send(pixels)
    

# okay, now get the picture we want to send them

img = gd.image('Xzibit_2007.jpg')

# Let's try to make a connection

for res in socket.getaddrinfo(host, port, socket.AF_UNSPEC,
                              socket.SOCK_STREAM, 0, socket.AI_PASSIVE):
    af, socktype, proto, canonname, sa = res
    try:
        s = socket.socket(af, socktype, proto)
    except socket.error, msg:
        s = None
        continue
    try:
        s.bind(sa)
        s.listen(1)
    except socket.error, msg:
        s.close()
        s = None
        continue
    break

# failed?

if s is None:
    print 'could not open socket'
    sys.exit(1)

print 'Listening on port ',port

conn, addr = s.accept()
print 'Connected by', addr

print 'Sending handshake'
conn.send('RFB 003.008\n')
handshake=''
while not handshake.endswith(chr(0)):
    handshake += conn.recv(1)
    print ord(handshake[-1])
print 'Client said: ',handshake

# Send security types we accept
print 'Sending security types'
conn.send(chr(1)) # One type
conn.send(chr(1)) # It's "none"
security = read8(conn)

print 'Client says: ',security
conn.send(chr(0)*4) # Okay

print 'Initialisation phase'

exclusive = conn.recv(1)
print 'Client wants exclusive access?', ord(exclusive)

(width, height) = img.size()

name = 'Multiplexer'

send16(conn, width)
send16(conn, height)
send8(conn, 32) # bits per pixel
send8(conn, 8) # depth
send8(conn, 1) # endianness
send8(conn, 1) # true colour
send16(conn, 255) # max red
send16(conn, 255) # max green
send16(conn, 255) # max blue
send8(conn, 16) # red shift
send8(conn, 8) # green shift
send8(conn, 0) # blue shift
conn.send(chr(0) + chr(0) + chr(0)) # padding
send32(conn, len(name))
conn.send(name)

print 'Sent server initialisation'

while 1:
    client_msg = conn.recv(1)
    if not client_msg:
        continue

    client_msg = ord(client_msg[0])
    print 'Remote said: ',client_msg
    if client_msg == 0:
        # SetPixelFormat
        # just eat it up for now
        for i in range(0, 19):
            print 'One pixel format byte is ',read8(conn)
    elif client_msg == 2:
        # SetEncodings
        read8(conn) # skip padding
        count = read16(conn)
        print 'There are ',count, 'encodings'

        for i in range(0, count):
            encoding = read32(conn)
            print 'One is ',encoding

        print "That's all"
    elif client_msg == 3:
        # FramebufferUpdateRequest
        incremental = read8(conn)
        x = read16(conn)
        y = read16(conn)
        w = read16(conn)
        h = read16(conn)
        print 'Getting: ',x,y,w,h
        send_image(conn, img)
    elif client_msg == 4:
        # KeyEvent
        down = read8(conn)
        read16(conn) # padding
        key = read32(conn)

        if down:
            print 'You pressed ',key
        else:
            print 'You released ',key
    elif client_msg == 5:
        # PointerEvent
        buttons = read8(conn)
        x = read16(conn)
        y = read16(conn)
        #print 'Pointer at ',x, y
    else:
        print 'Unknown command: ',client_msg

print 'DONE.'
conn.close()
