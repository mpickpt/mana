import subprocess
import re
import sys
import threading
import socket
import argparse

from subprocess import PIPE

outlock = threading.Lock()


def parse_ranks(mana_status_path):
    ranks = {}
    stat = subprocess.run([f'{mana_status_path}'], stdout=PIPE)
    stat.check_returncode()
    rank_arr = stat.stdout.decode('utf-8').split('\n')
    rank_arr = rank_arr[5:len(rank_arr)-1]
    for rank in rank_arr:
        r_ind = rank[0:rank.index(',')]
        ranks[r_ind] = {}
        rank = rank[rank.index(',')+1:]
        ranks[r_ind]['host'] = rank[rank.index('@')+1:rank.index(',')]
        ranks[r_ind]['pid'] = rank[rank.index(':')+1:rank.index(']')]
    return ranks


def attach_to_rank(rank, pid, host, infile, outfile):
    proc = subprocess.Popen(['ssh', f'{host}'], stdout=PIPE, stdin=PIPE,
                            stderr=PIPE)
    out, errs = proc.communicate(f'gdb attach {pid} -batch -x={infile}'.
                                 encode())
    outlock.acquire()
    file = open(outfile, 'a')
    file.write(f'RANK {rank}:\n')
    file.write(out.decode('utf-8'))
    file.write('=================================================\n\n')
    file.close()
    outlock.release()

class CustomParser(argparse.ArgumentParser):
    def error(self, message):
        sys.stderr.write('error: %s\n' % message)
        self.print_help()
        sys.exit(2)

def main():
    parser = CustomParser(description='Attach GDB to all ranks')
    parser.add_argument('infile',metavar='I', help='GDB command file')
    parser.add_argument('outfile', metavar='O', help='Output log file')
    parser.add_argument('-m','--mana_status', metavar='M', help='Absolute path\
                         to mana_status executable', default='mana_status',
                         required=False)
    parser.add_argument('-s','--skip', help='Skip one rank to debug manually',
                         action='store_true')

    args = parser.parse_args()
    infile = args.infile
    outfile = args.outfile
    skipped = not args.skip
    mana_status_path = args.mana_status

    file = open(outfile, "w+")
    file.truncate(0)
    file.close()

    ranks = parse_ranks(mana_status_path)
    threads = []
    for rank in ranks:
        pid = ranks[rank]['pid']
        host = ranks[rank]['host']
        if not skipped and socket.gethostname() == host:
            print(f"Skipping {pid} on {host} for manual debugging")
            skipped = True
            continue

        th = threading.Thread(target=attach_to_rank, args=(rank, pid, host,
                                                           infile, outfile,))
        th.start()

    for thread in threads:
        thread.join()

if __name__ == '__main__':
    main()
