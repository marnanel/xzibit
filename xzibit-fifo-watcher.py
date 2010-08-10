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

while True:
    channel = f.read(1)+f.read(1)*256
    length = f.read(1)+f.read(1)*256

    print '%d bytes on channel %x' % (length, channel)
    f.read(b)

