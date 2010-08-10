# While we're transitioning to sending everything over
# a multiplexed TCP stream, there's two intermediate
# stages:
#
#  1) using a fifo instead of TCP and having a dummy
#     client on the end of the fifo
#  2) using the real client, over a fifo
#
# This is that dummy client.

f = file('/tmp/xzibit-fifo')

header = f.read(12)

if header=='Xz 000.001\r\n':
    print 'Header received correctly'
else:
    print 'Header NOT received correctly'

while True:
    channel = f.read(1)+f.read(1)*256
    length = f.read(1)+f.read(1)*256

    print '%d bytes on channel %x' % (length, channel)
    f.read(b)

