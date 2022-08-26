#!/usr/bin/env python3

import sys
import os
import subprocess
import time
import argparse
from threading import Thread
from subprocess import PIPE

verbose = False

# Wrapper to eprint to stderr
def eprint(verbose, args):
    if verbose:
        print(args, file=sys.stderr)

# Custom argparser to eprint error message
class CustomParser(argparse.ArgumentParser):
    def error(self, message):
        sys.stderr.write('error: %s\n' % message)
        self.print_help()
        os._exit(2)

# Error handling thread for timeouts
def timeout_exit(timeout):
    time.sleep(int(timeout))
    sys.stderr.write('ERROR: TEST FAILED BY TIMEOUT\n')
    sys.stderr.flush()
    os._exit(11)

# Remove unnecessary files (old checkpoint files)
def clean():
    subprocess.run('rm -rf ckpt_rank_*', shell=True)
    subprocess.run('rm -rf dmtcp_restart_*', shell=True)

# Helper function to get the number of currently running ranks
def get_running_ranks(mana_dir, state=None):
    status = subprocess.run([f"{mana_dir}/bin/mana_status"], stdout=PIPE,
                             stderr=subprocess.DEVNULL)
    if state is None:
        return status.stdout.decode('utf-8').count(".mana.exe")
    else:
        return status.stdout.decode('utf-8').count(state)

'''
Example usage:

python3 run_mana_mpi_regression_test.py send_recv_loop -i 12 -n 3 -m ~/mana

'''
def main():
    # Parse arguments
    parser = CustomParser(description='Run a MANA Test')
    parser.add_argument('-i', '--iterations', metavar='I',
                         help='Number of iterations for the test case',
                         required=True)
    parser.add_argument('test', metavar='T', help='Path to test case to run')
    parser.add_argument('-n','--num_ranks', metavar='N', help='Number of ranks \
                              for test', required=True)
    parser.add_argument('-m','--mana_root', metavar='M', help='Absolute \
                         path to mana folder', default='', required=False)
    parser.add_argument('-r','--mpirun', help='Use mpirun instead \
                         of srun', action="store_true")
    parser.add_argument('-v','--verbose', help='Print status logs to console',
                        action="store_true")
    parser.add_argument('-a', '--args', help='Arguments to pass to test',
                        default='')
    parser.add_argument('-t', '--timeout', help='Testcase time (seconds)')
    args = parser.parse_args()
    name = args.test
    itr = args.iterations
    ranks = args.num_ranks
    mana_dir = args.mana_root
    if mana_dir == '':
        mana_dir = os.path.abspath(__file__).replace('/mpi-proxy-split/'
                                                     'test/run_all.py', '')
    verbose = args.verbose
    srun = ''
    restart='srun'
    if args.mpirun:
        srun='-r'
        restart='mpirun'
    add_args = args.args
    mana_bin = f'{mana_dir}/bin'

    # Set timeout as thread
    if not args.timeout == None:
        eprint(verbose, "Setting timeout...")
        t = Thread(target=timeout_exit, args=(args.timeout, ))
        t.start()
        eprint(verbose,"Timeout set...")

    eprint(verbose, f"Running {name} for {itr} iterations with {ranks} ranks")

    # Start test by running mana_coordinator and executable
    clean()
    kill_child = subprocess.Popen(['pkill', '-9', 'dmtcp_coord'])
    kill_child.wait()
    eprint(verbose, "Starting test...")
    cmd=''
    if add_args == '':
        cmd = (f'python3 {mana_dir}/mpi-proxy-split/test/mana_test.py '
               f'{mana_dir}/mpi-proxy-split/test/{name} -m {mana_bin} '
               f'{srun} -n {ranks} -i {itr}')

    else:
        cmd = (f'python3 {mana_dir}/mpi-proxy-split/test/mana_test.py '
               f'{mana_dir}/mpi-proxy-split/test/{name} -m {mana_bin} '
               f'{srun} -n {ranks} -i {itr} -a {add_args}')
    eprint(verbose, cmd)
    run_child = subprocess.Popen([cmd], shell=True)

    # Wait for ranks to start
    while get_running_ranks(mana_dir, "WorkerState::RUNNING") != int(ranks):
        time.sleep(0.5)

    print("Running")

    # Test executable starting, checkpoint 3 times
    eprint(verbose, "Test started...")
    for i in range(3):
        time.sleep(3)
        if run_child.poll() is None:
            # Send checkpoint command and wait on it to succeed
            eprint(verbose, "Sending checkpoint command...")
            ckpt_child = subprocess.run([f'{mana_dir}/bin/mana_status',
                                         '--checkpoint'])
            if ckpt_child.check_returncode() is not None:
                eprint(verbose, "Test case failed: checkpointing failed")
                return 1
            eprint(verbose, "Checkpoint command successful")

            # Wait for checkpoint to complete, make sure no ranks die
            while get_running_ranks(mana_dir, "WorkerState::RUNNING") \
                    != int(ranks):
                if get_running_ranks(mana_dir) != int(ranks):
                    eprint(verbose, "Test case failed: not enough \
                        ranks running while checkpointing")
                    return 6

            # Kill DMTCP Coord
            kill_child = subprocess.Popen(['pkill', '-9', 'dmtcp_coord'])
            kill_child.wait()

            # Wait for DMTCP Coord to die, then restart it
            status = subprocess.run([f'{mana_dir}/bin/mana_status'],
                                     stdout=PIPE, stderr=subprocess.DEVNULL)
            while "Checking for coordinator" \
                    not in status.stdout.decode('utf-8'):
                status = \
                    subprocess.run([f'{mana_dir}/bin/mana_status'],
                                    stdout=PIPE, stderr=PIPE)
            eprint(verbose, "Checkpointed, restarting...")
            subprocess.run([f"{mana_dir}/bin/mana_coordinator"],
                           stderr=subprocess.DEVNULL, stdout=subprocess.DEVNULL)

            # Wait for coord to restart, then restart ranks
            time.sleep(1)
            run_child = subprocess.Popen([f'{restart}', '-n', f'{ranks}',
                                         f'{mana_dir}/bin/mana_restart'],
                                         stdout=subprocess.DEVNULL,
                                         stderr=subprocess.DEVNULL)

            # Wait for ranks to restart without dying
            num_seen = 0
            while get_running_ranks(mana_dir, "WorkerState::RUNNING") \
                    != int(ranks):
                if get_running_ranks(mana_dir) < num_seen:
                    eprint(verbose, "Test case failed: not enough \
                        ranks running after restart")
                    return 2
                num_seen = get_running_ranks(mana_dir)

            time.sleep(3) # Sleep between checkpoints to allow for progression

            clean() # Remove ckpt images and restart scripts
        else: # Child died earlier, check retval to see if successful
            if run_child.poll() != 0:
                eprint(verbose, "Test case failed: child exited early")
                return 3
            else:
                eprint(verbose, "Child exited early: increase iterations")
                return 0

    # Check enough ranks running at exit, then return
    # Kills mana_coordinator + spawned MPI children
    if get_running_ranks(mana_dir) != int(ranks):
        eprint(verbose, "Test case failed: not enough ranks "
               "running when exiting")
        run_child.kill()
        return 4
    else:
        eprint(verbose, "Test case passed")
        run_child.kill()
        return 0

if __name__ == "__main__":
    ecode = main()
    if ecode == 0:
        sys.stderr.write('TESTCASE PASSED\n')
    else:
        sys.stderr.write('TESTCASE FAILED with code %d\n' % ecode)
    sys.stderr.flush()
    os._exit(ecode)
