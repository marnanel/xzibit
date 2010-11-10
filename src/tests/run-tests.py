import random
import os.path
import sys
import time
import subprocess

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

        self._tasks = {}

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
                           compare = '-Tv',
                           expectations = {'compare': 0},
                           end_after = ['compare'])

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
                      compare=None,
                      expectations={},
                      end_after=[]):
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

        self._clear_up(expectations,
                       end_after)

    def _x_display_is_in_use(self, display):
        return os.path.exists('/tmp/.X%d-lock' % (display,))

    def _unused_x_display(self):
        display = int(random.random()*100)
        while self._x_display_is_in_use(display):
            display += 1
        return display

    def _run(self, *args):

        params = [self._programs[args[0]]]
        params.extend(args[1:])

        popen = subprocess.Popen(params)

        self._tasks[args[0]] = popen

    def _clear_up(self, expectations, end_after):

        running = True
        success = None

        while self._tasks and running:
            # Now we could do something clever with signals
            # here, but this is just a test which runs for
            # at most ten seconds, so it's sufficient
            # to poll each status once a second.
            removals = []
            for task in self._tasks:
                code = self._tasks[task].poll()

                if code is not None:
                    partial_success = self._handle_program_ending(task,
                                                                  code,
                                                                  expectations)

                    if partial_success is not None:
                        success = partial_success

                    removals.append(task)

            for task in removals:
                del self._tasks[task]
                if task in end_after:
                    running = False
            time.sleep(1)

        # So nothing is left but to clear up the pieces.
        for task in self._tasks:
            self._tasks[task].send_signal(15) # SIGTERM
        self._tasks = {}

        if success is None:
            success = 'fail'

        print '>>> RESULT: ', success
        sys.exit(1)

    def _handle_program_ending(self, program, code, expectations):

        """Handles one of our programs terminating.  Returns None
        if we can't say whether this makes a success or failure,
        'pass' if it's a success, or 'fail' if it's a failure."""

        if expectations.has_key(program) and expectations[program]==code:
            print '(Program %s ended as expected)' % (program,)
            return 'pass'

        print '*** Program %s has ended with unexpected code %d ***' % (program,
                                                                        code)

        return 'fail'

if __name__=='__main__':
    tests = Tests()

    tests.run_all()

