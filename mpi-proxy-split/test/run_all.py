#!/usr/bin/env python3

import subprocess
import argparse
import sys
import os

deep_tests = {
    'Allgather_test': {'itr': 1000000, 'ranks': 4},
    'Alloc_mem': {'itr': 20, 'ranks': 2},
    'Allreduce_test': {'itr': 1000000000, 'ranks': 4},
    'Alltoall_test': {'itr': 1000000000, 'ranks': 4},
    'Alltoallv_test': {'itr': 1000000000, 'ranks': 4},
    'Barrier_test': {'itr': 12, 'ranks': 4},
    'Bcast_test': {'itr': 800000, 'ranks': 4},
    'Cart_map_test': {'itr': 100000000, 'ranks': 4},
    'Cart_sub_test': {'itr': 100000000, 'ranks': 6},
    'Cartdim_get_test': {'itr': 100000000, 'ranks': 12},
    'Comm_dup_test': {'itr': 60, 'ranks': 4},
    'Comm_get_attr_test': {'itr': 60, 'ranks': 2},
    'Comm_split_test': {'itr': 60, 'ranks': 4},
    'Gather_test': {'itr': 100000000, 'ranks': 3},
    'Gatherv_test': {'itr': 100000000, 'ranks': 4},
    'Group_size_rank': {'itr': 100000000, 'ranks': 4},
    'Ibarrier_test': {'itr': 10, 'ranks': 4},
    'Ibcast_test': {'itr': 100000000, 'ranks': 4},
    'Ibcast_test1': {'itr': 100000000, 'ranks': 4, 'args': -1},
    'Ibcast_test2': {'itr': 100000000, 'ranks': 4, 'args': -2},
    'Initialized_test': {'itr': 100000000, 'ranks': 4},
    'Irecv_test': {'itr': 12, 'ranks': 2},
    'Isend_test': {'itr': 12, 'ranks': 2},
    'Reduce_test': {'itr': 100000000, 'ranks': 4},
    'Scan_test': {'itr': 100000000, 'ranks': 4},
    'Scatter_test': {'itr': 100000000, 'ranks': 3},
    'Scatter_test': {'itr': 100000000, 'ranks': 4},
    'Sendrecv_test': {'itr': 100000000, 'ranks': 3},
    'Testany_test': {'itr': 10, 'ranks': 3},
    'Type_commit_contiguous': {'itr': 10, 'ranks': 4},
    'Type_hvector_test': {'itr': 100000000, 'ranks': 2},
    'Type_vector_test': {'itr': 100000000, 'ranks': 2},
    'Waitall_test': {'itr': 10, 'ranks': 4},
    'Waitany_test': {'itr': 10, 'ranks': 4},
    'file_test': {'itr': 100000000, 'ranks': 2},
    'keyval_test': {'itr': 12, 'ranks': 4},
    'large_async_p2p': {'itr': 12, 'ranks': 2},
    'send_recv_loop': {'itr': 12, 'ranks': 3},
    'sendrecv_replace_test': {'itr': 100000000, 'ranks': 3},
    'two-phase-commit-1': {'itr': 60, 'ranks': 4},
    'two-phase-commit-2': {'itr': 60, 'ranks': 4}
}

shallow_tests = {
    'Allgather_test': {'itr': 10, 'ranks': 4},
    'Alloc_mem': {'itr': 5, 'ranks': 2},
    'Allreduce_test': {'itr': 100, 'ranks': 4},
    'Alltoall_test': {'itr': 100, 'ranks': 4},
    'Alltoallv_test': {'itr': 100, 'ranks': 4},
    'Barrier_test': {'itr': 12, 'ranks': 4},
    'Bcast_test': {'itr': 20, 'ranks': 4},
    'Cart_map_test': {'itr': 10, 'ranks': 4},
    'Cart_sub_test': {'itr': 10, 'ranks': 6},
    'Cartdim_get_test': {'itr': 10, 'ranks': 12},
    'Comm_dup_test': {'itr': 10, 'ranks': 4},
    'Comm_get_attr_test': {'itr': 10, 'ranks': 2},
    'Comm_split_test': {'itr': 10, 'ranks': 4},
    'Gather_test': {'itr': 10, 'ranks': 3},
    'Gatherv_test': {'itr': 10, 'ranks': 4},
    'Group_size_rank': {'itr': 10, 'ranks': 4},
    'Ibarrier_test': {'itr': 4, 'ranks': 4},
    'Ibcast_test': {'itr': 10, 'ranks': 4},
    'Ibcast_test1': {'itr': 10, 'ranks': 4, 'args': -1},
    'Ibcast_test2': {'itr': 10, 'ranks': 4, 'args': -2},
    'Initialized_test': {'itr': 10, 'ranks': 4},
    'Irecv_test': {'itr': 4, 'ranks': 2},
    'Isend_test': {'itr': 4, 'ranks': 2},
    'Reduce_test': {'itr': 10, 'ranks': 4},
    'Scan_test': {'itr': 10, 'ranks': 4},
    'Scatter_test': {'itr': 10, 'ranks': 3},
    'Scatter_test': {'itr': 10, 'ranks': 4},
    'Sendrecv_test': {'itr': 10, 'ranks': 3},
    'Testany_test': {'itr': 4, 'ranks': 3},
    'Type_commit_contiguous': {'itr': 4, 'ranks': 4},
    'Type_hvector_test': {'itr': 10, 'ranks': 2},
    'Type_vector_test': {'itr': 10, 'ranks': 2},
    'Waitall_test': {'itr': 2, 'ranks': 4},
    'Waitany_test': {'itr': 2, 'ranks': 4},
    'file_test': {'itr': 10, 'ranks': 2},
    'keyval_test': {'itr': 4, 'ranks': 4},
    'large_async_p2p': {'itr': 4, 'ranks': 2},
    'send_recv_loop': {'itr': 4, 'ranks': 3},
    'sendrecv_replace_test': {'itr': 10, 'ranks': 3},
    'two-phase-commit-1': {'itr': 20, 'ranks': 4},
    'two-phase-commit-2': {'itr': 20, 'ranks': 4}
}


# Custom argparser to eprint error message
class CustomParser(argparse.ArgumentParser):
    def error(self, message):
        sys.stderr.write('error: %s\n' % message)
        self.print_help()
        os._exit(2)
'''
Example usage:

python3 run_all.py -r -m ~/mana

'''
def main():
    parser = CustomParser(description='Run all MANA Tests')
    parser.add_argument('-m','--mana_root', metavar='M', help='Absolute \
                         path to mana folder', default='', required=False)
    parser.add_argument('-r','--mpirun', help='Use mpirun instead \
                         of srun', action="store_true")
    parser.add_argument('-s', '--shallow', help='Use a shallow test profile, run every test just once', action="store_true")

    args = parser.parse_args()
    mana = args.mana_root
    if mana == '':
        mana = os.path.abspath(__file__).replace('/mpi-proxy-split/'
                                                 'test/run_all.py', '')
    mpirun = ''
    if args.mpirun:
        mpirun = '-r '
    num_tests = 0
    num_passed = 0
    failed = []
    tests = shallow_tests if args.shallow else deep_tests

    for test in tests:
        print(f'Running test {test}:', end=" ")
        sys.stdout.flush()
        i = tests[test]['itr']
        n = tests[test]['ranks']
        if 'args' in tests[test]:
            a = tests[test]['args']
            cmd = (f'python3 {mana}/mpi-proxy-split'
                   f'/test/run_mana_mpi_regression_test.py {mpirun}-m {mana}'
                   f' -t 120 -i {i} -n {n} -a={a} {test}')
        else:
            cmd = (f'python3 {mana}/mpi-proxy-split'
                   f'/test/run_mana_mpi_regression_test.py {mpirun}-m {mana}'
                   f' -t 120 -i {i} -n {n} {test}')
        test_child = subprocess.run([cmd], shell=True,
                                    stderr=subprocess.DEVNULL,
                                    stdout=subprocess.DEVNULL)
        try:
            r = test_child.check_returncode()
            print('PASSED')
            num_passed += 1
        except:
            print('FAILED')
            failed.append(test)
        num_tests += 1
    print("TESTS COMPLETED")
    print(f'Passed {num_passed} of {num_tests}')
    print('Failed tests: ')
    print(failed)

if __name__ == '__main__':
    main()
