import random
import os.path
import sys
import time

# TODO:
#  - _run() needs to store the pid
#    somewhere so we can kill things when we're
#    done with them.
#  - more importantly, we need a way to make it
#    tell us when one of the processes quits.

class Tests:
    def __init__(self):
        self._programs = {
            'xephyr': 'Xephyr',
            'autoshare': 'xzibit-autoshare',
            'compare': 'xzibit-test-compare',
            'mutter': 'mutter',
            }

        # now make sure we can find them all

        search_path = os.getenv('PATH', '').split(':')
        search_path.insert(0, '.')
        search_path.insert(0, '..')
        search_path.insert(0, 'src')
        search_path.insert(0, 'src/tests')
        all_found_so_far = 1

        for program in self._programs:

            found = 0
            
            for section in search_path:
                progname = self._programs[program]

                path = os.path.join(section, progname)
                if os.path.exists(path):
                    found = 1
                    self._programs[program] = path
                    break

            if not found:
                if all_found_so_far:
                    print 'The following programs are needed, but were not found:'
                    all_found_so_far = 0

                print '\t%s' % (progname,)

                if progname.startswith('xzibit'):
                    print '\t\tSince this is an xzibit program, it seems possible'
                    print '\t\tthat you have not yet run ./configure and make.'

        if not all_found_so_far:
            sys.exit(3)

    def run_all(self):
        for test in sorted(dir(self)):
            if test.startswith('test'):
                func = getattr(self, test)
                print '%s - %s' % (test,
                                   func.__doc__)
                func()

    def test010(self):
        "Titles of both windows are the same"
        self._general_test(autoshare = '',
                           compare = '-Tv')

    def test020(self):
        "Contents of both windows are the same"
        pass

    def test030(self):
        "Sending keystrokes works"
        pass

    def test040(self):
        "Sending mouse clicks works"
        pass

    def _general_test(self,
                      autoshare=None,
                      compare=None):
        # FIXME: This routine tries to make sure that
        # things have settled after launching a program
        # by simply waiting.  It might be better to
        # parse the output of each program somehow.
        display = self._unused_x_display()
        self._run('xephyr',
                  ':%d' % (display,))
        os.putenv('DISPLAY',
                  ':%d.0' % (display,))
        time.sleep(1)

        self._run('mutter')
        time.sleep(1)

        if autoshare is not None:
            self._run('autoshare',
                      autoshare,
                      '-L')
            # this takes a while, so:
            time.sleep(5)

        if compare is not None:
            self._run('compare',
                      compare)

    def _x_display_is_in_use(self, display):
        return os.path.exists('/tmp/.X%d-lock' % (display,))

    def _unused_x_display(self):
        display = int(random.random()*100)
        while self._x_display_is_in_use(display):
            display += 1
        return display

    def _run(self, *args):
        os.spawnv(os.P_NOWAIT,
                  self._programs[args[0]],
                  args)

if __name__=='__main__':
    tests = Tests()

    tests.run_all()

