#!/bin/bash
set -x

SCRIPT_DIR="$(dirname $(readlink -f "$0"))"

cd $SCRIPT_DIR/..

./configure
make clean
make -j 8 mana

cd $SCRIPT_DIR/../contrib/mpi-proxy-split/unit-test
make
make clean
make check || exit 1
