import random
import os.path
import sys

class Tests:
    def __init__(self):
        self._programs = {
            'xephyr': 'Xephyr',
            'autoshare': 'xzibit-autoshare',
            }

        # now make sure we can find them all

        # FIXME: path needs to have some places prepended,
        # so we get to search in the current dir and the
        # tests dir, etc.

        search_path = os.getenv('PATH', '').split(':')
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

        print self._programs

        # FIXME: complain about any we didn't find
        # (actually, do this in the loop)
        
        sys.exit(1)

    def run_all(self):
        for test in sorted(dir(self)):
            if test.startswith('test'):
                func = getattr(self, test)
                print '%s - %s' % (test,
                                   func.__doc__)
                func()

    def test010(self):
        "Titles of both windows are the same"
        self._general_test(autoshare = '')

    def test020(self):
        "Contents of both windows are the same"
        pass

    def test030(self):
        "Sending keystrokes works"
        pass

    def test040(self):
        "Sending mouse clicks works"
        pass

    def _general_test(autoshare=None):
        self._run('xephyr',
                  ':%d' % (display,))
        print 

    def _x_display_is_in_use(self, display):
        return os.path.exists('/tmp/.X%d-lock' % (display,))

    def _unused_x_display(self):
        display = int(random.random()*100)
        while x_display_is_in_use(display):
            display += 1
        return display

    def _run(self, *args):
        # FIXME: check args[0] exists, bail if not
        os.spawnv(os.P_NOWAIT, args[0], args)

if __name__=='__main__':
    tests = Tests()

    tests.run_all()

