#!/usr/bin/env python3

import argparse
import sys
import subprocess

'''

This util is designed to be an argument parsing utility for C/C++ tests.

'''

class CustomParser(argparse.ArgumentParser):
    def error(self, message):
        sys.stderr.write('error: %s\n' % message)
        self.print_help()
        sys.exit(2)

def main():
    parser = CustomParser(description='Run a MANA Test')
    parser.add_argument('-i', '--iterations', metavar='I',
                         help='Number of iterations for test')
    parser.add_argument('test', metavar='T', help='Path to test case to run')
    parser.add_argument('-n','--num_ranks', metavar='N', help='Number of ranks\
                              for test', required=True)
    parser.add_argument('-m','--mana_bin', metavar='M', help='Absolute \
                         path to mana_bin folder', default=
                         '', required=False)
    parser.add_argument('-r','--mpirun', help='Use mpirun instead\
                         of srun', action="store_true")
    parser.add_argument('-a', '--args', help='Arguments to pass to test',
                        default='')

    args = parser.parse_args()
    if args.mana_bin == '':
        mana_coordinator_path = f'mana_coordinator'
        mana_launch_path = f'mana_launch'
    else:
        mana_coordinator_path = f'{args.mana_bin}/mana_coordinator'
        mana_launch_path = f'{args.mana_bin}/mana_launch'

    print(f'{mana_coordinator_path}')
    coord_child = subprocess.run([f'{mana_coordinator_path}'])
    run = 'srun'
    if args.mpirun:
        run = 'mpirun'

    if args.iterations == None:
        print(f'{run} -n {args.num_ranks} {mana_launch_path} '
               f'{args.test}.mana.exe {arg.args}')
        test_child = subprocess.run([f'{run}', '-n', f'{args.num_ranks}',
                                 f'{mana_launch_path}',
                                 f'{args.test}.mana.exe'
                                 f'{args.args}'], stdout = subprocess.DEVNULL)
    else:
        if args.args == '':
            print(f'{run} -n {args.num_ranks} {mana_launch_path} '
                f'{args.test}.mana.exe {args.iterations} {args.args}')
            test_child = subprocess.run([f'{run}', '-n', f'{args.num_ranks}',
                                    f'{mana_launch_path}',
                                    f'{args.test}.mana.exe',
                                    f'{args.iterations}', f'{args.args}'],
                                    stdout = subprocess.DEVNULL)
        else:
            print(f'{run} -n {args.num_ranks} {mana_launch_path} '
                f'{args.test}.mana.exe -i {args.iterations} {args.args}')
            test_child = subprocess.run([f'{run}', '-n', f'{args.num_ranks}',
                                    f'{mana_launch_path}',
                                    f'{args.test}.mana.exe', '-i'
                                    f'{args.iterations}', f'{args.args}'],
                                    stdout = subprocess.DEVNULL)


if __name__ == "__main__":
    main()