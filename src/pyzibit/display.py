import random
import os.path
import sys

def x_display_is_in_use(display):
    return os.path.exists('/tmp/.X%d-lock' % (display,))

def unused_x_display():
    display = int(random.random()*100)
    while x_display_is_in_use(display):
        display += 1
    return display

def run(args):
    # FIXME: check args[0] exists, bail if not
    os.spawnv(os.P_NOWAIT, args[0], args)

def display(visible=True):
    display = unused_x_display()
    print display

    if visible:
        run(['/usr/bin/Xephyr',
             ':%d' % (display,)])
    else:
        print "Don't know how to do invisible yet"
        sys.exit(255)

    os.putenv('DISPLAY',
              ':%d.0' % (display,))

    run(['/usr/bin/mutter'])

    run(['/usr/local/bin/xzibit-autoshare', '-L'])

if __name__=='__main__':
    display()
