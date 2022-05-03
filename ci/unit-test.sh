#!/bin/bash
set -x

SCRIPT_DIR="$(dirname $(readlink -f "$0"))"

cd $SCRIPT_DIR/..

OS_VER=$(grep '^VERSION_ID' /etc/os-release | sed 's/"//g'| cut -d "=" -f2)
#GCC 8 or higher
if [[ $OS_VER == 7 ]]; then
  source scl_source enable devtoolset-8
fi

git submodule update --init

./configure
make -j 8 mana
if [ \! -f "bin/dmtcp_launch" ];then echo ERROR: make failed;exit 1;fi

cd $SCRIPT_DIR/../mpi-proxy-split/unit-test
make || exit 1
make clean
make check || exit 1
