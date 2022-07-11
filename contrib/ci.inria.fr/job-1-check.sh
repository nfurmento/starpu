#!/bin/bash
# StarPU --- Runtime system for heterogeneous multicore architectures.
#
# Copyright (C) 2013-2022  Université de Bordeaux, CNRS (LaBRI UMR 5800), Inria
#
# StarPU is free software; you can redistribute it and/or modify
# it under the terms of the GNU Lesser General Public License as published by
# the Free Software Foundation; either version 2.1 of the License, or (at
# your option) any later version.
#
# StarPU is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
#
# See the GNU Lesser General Public License in COPYING.LGPL for more details.
#

set -e
set -x

ulimit -c unlimited

export PKG_CONFIG_PATH=/home/ci/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH
export LD_LIBRARY_PATH=/home/ci/usr/local/lib:$LD_LIBRARY_PATH

tarball=$(ls -tr starpu-*.tar.gz | tail -1)

if test -z "$tarball"
then
    echo Error. No tar.gz file
    ls
    pwd
    exit 1
fi

COVERITY=0
if test "$1" == "-coverity"
then
    COVERITY=1
    if test -f $HOME/.starpu/coverity_token
    then
	COVERITY_TOKEN=$(cat $HOME/.starpu/coverity_token)
    else
	echo "Error. Coverity is enabled, but there is no file $HOME/.starpu/coverity_token"
	exit 1
    fi
fi

basename=$(basename $tarball .tar.gz)
export STARPU_HOME=$PWD/$basename/home
mkdir -p $basename
cd $basename
(
    echo "oldPWD=\${PWD}"
    env|grep -v LS_COLORS | grep '^[A-Z]'|grep -v BASH_FUNC | grep '=' | sed 's/=/=\"/'| sed 's/$/\"/' | sed 's/^/export /'
    echo "cd \$oldPWD"
) > ${PWD}/env

test -d $basename && chmod -R u+rwX $basename && rm -rf $basename
tar xfz ../$tarball

hour=$(date "+%H")
today=$(date "+%Y-%m-%d")
lasthour=$(echo $hour - 1 | bc )
if test "$hour" = "0" -o "$hour" = "00"
then
    lasthour=0
fi

find $basename -exec touch -d ${today}T${lasthour}:0:0 {} \; || true

cd $basename
BUILD=./build_$$
mkdir $BUILD
cd $BUILD

STARPU_CONFIGURE_OPTIONS=""
suname=$(uname)
if test "$suname" = "Darwin"
then
    STARPU_CONFIGURE_OPTIONS="--without-hwloc"
fi
if test "$suname" = "OpenBSD"
then
    STARPU_CONFIGURE_OPTIONS="--without-hwloc --disable-mlr --enable-maxcpus=2"
fi
if test "$suname" = "FreeBSD"
then
    STARPU_CONFIGURE_OPTIONS="--disable-fortran --enable-maxcpus=2"
fi

export CC=gcc

set +e
mpiexec -oversubscribe pwd 2>/dev/null
ret=$?
set -e
ARGS=""
if test "$ret" = "0"
then
    ARGS="--with-mpiexec-args=-oversubscribe"
fi

CONFIGURE_OPTIONS="--enable-debug --enable-verbose --enable-mpi-check=maybe --disable-build-doc $ARGS"
CONFIGURE_CHECK=""
day=$(date +%u)
if test $day -le 5
then
    CONFIGURE_CHECK="--enable-quick-check"
#else
    # we do a normal check, a long check takes too long on VM nodes
fi
../configure $CONFIGURE_OPTIONS $CONFIGURE_CHECK  $STARPU_CONFIGURE_OPTIONS $STARPU_USER_CONFIGURE_OPTIONS

export STARPU_TIMEOUT_ENV=1800
export MPIEXEC_TIMEOUT=1800

if test "$COVERITY" == "1"
then
    cov-build --dir cov-int make -j4
    grep "are ready for analysis" cov-int/build-log.txt
    tar caf starpu.tar.xz cov-int
    curl -k -f --form token=$COVERITY_TOKEN --form email=starpu-builds@inria.fr --form file=@starpu.tar.xz --form version=master --form description="https://scan.coverity.com/builds?project=StarPU"
    exit 0
fi

make -j4
make dist
set +e
set -o pipefail
make -k check 2>&1 | tee  ../check_$$
RET=$?
set +o pipefail
set -e
make showsuite

make clean

grep "^FAIL:" ../check_$$ || true

echo "Running on $(uname -a)"
exit $RET
