#!/usr/bin/python
#
# This mimics what xzibit-plugin sends to xzibit-rfb-client.
#
# Run it thus:
#    fake-xzibit.py | xzibit-rfb-client -f 0

import sys

message = [

    # Open channel 1
    0, 0, 3, 0, 1, # Open
    1, 0,

]

for byte in message:
    sys.stdout.write(chr(byte))
    sys.stdout.flush()
