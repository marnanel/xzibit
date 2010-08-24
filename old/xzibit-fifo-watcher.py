# While we're transitioning to sending everything over
# a multiplexed TCP stream, there's two intermediate
# stages:
#
#  1) using a fifo instead of TCP and having a dummy
#     client on the end of the fifo
#  2) using the real client, over a fifo
#
# This is that dummy client.

import sys

f = file('/tmp/xzibit-fifo')

def receive():
    c = ord(f.read(1))
    if c=='':
        print 'The other side has died.  Aborting.'
        sys.exit(1)
    #print 'GOT: %x' % (c,)
    return c

header = f.read(12)
correct_header = 'Xz 000.001\r\n'

if header==correct_header:
    print 'Header received correctly'
else:
    print 'Header NOT received correctly: got %s, wanted %s' % (
        header, correct_header)

while True:
    channel = receive()+receive()*256
    length = receive()+receive()*256

    print '%d bytes on channel %x' % (length, channel)
    buf = []
    for i in range(0,length):
        buf.append(receive())
    if channel==0 and len(buf)!=0:
        opcode = buf[0]
        if opcode==1:
            print '(Open)'
        elif opcode==2:
            print '(Close)'
        elif opcode==3:
            print '(Set)'
        elif opcode==4:
            print '(Wall)'
        else:
            print '(Opcode is %d)' % (opcode)

