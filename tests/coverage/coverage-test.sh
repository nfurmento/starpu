#!/bin/bash

DIR=$PWD
ROOTDIR=$DIR/../..
COVDIR=coverage
MAXCPU=3

init()
{
	mkdir -p $COVDIR
	lcov --directory $COVDIR --zerocounters
}

save_cov()
{
	testname=$1
	lcov --directory $ROOTDIR --capture --output $COVDIR/$testname.info 
	lcov -a $COVDIR/$testname.info -o $COVDIR/all.info
}

generatehtml()
{
	cd $COVDIR
	genhtml all.info
	cd $DIR
}

cd $ROOTDIR
make clean 1> /dev/null 2> /dev/null
make examples -j ATLAS=1 CPUS=$MAXCPU CUDA=1 COVERAGE=1 1> /dev/null 2> /dev/null
cd $DIR

init;

echo "incrementer"
timing=`$ROOTDIR/examples/incrementer/incrementer 2> /dev/null`
save_cov "incrementer";

echo "tag_example"
timing=`$ROOTDIR/examples/tag_example/tag_example -iter 64 -i 128 -j 24 2> /dev/null`
save_cov "tag_example";

echo "spmv"
timing=`$ROOTDIR/examples/spmv/dw_spmv 2> /dev/null`
save_cov "spmv";

echo "spmv.gpu"
timing=`NCPUS=0 $ROOTDIR/examples/spmv/dw_spmv 2> /dev/null`
save_cov "spmv.gpu";


echo "strassen.ws"
timing=`SCHED="ws" $ROOTDIR/examples/strassen/dw_strassen -rec 3 -size 2048 -pin 2> /dev/null`
save_cov "strassen.ws";


echo "strassen.dm"
timing=`SCHED="dm" $ROOTDIR/examples/strassen/dw_strassen -rec 3 -size 2048 -pin 2> /dev/null`
save_cov "strassen.dm";


echo "strassen.dmda"
timing=`SCHED="dmda" $ROOTDIR/examples/strassen/dw_strassen -rec 3 -size 2048 -pin 2> /dev/null`
save_cov "strassen.dmda";


echo "chol"
timing=`$ROOTDIR/examples/cholesky/dw_cholesky 2> /dev/null`
save_cov "chol";

echo "heat.dm.4k.calibrate.v2"
timing=`CALIBRATE=1 SCHED="dm" $ROOTDIR/examples/heat/heat -ntheta 66 -nthick 66 -nblocks 4 -v2 -pin 2> /dev/null`
save_cov "heat.dm.4k.calibrate.v2";


echo "heat.dm.8k.calibrate.v2"
timing=`CALIBRATE=1 SCHED="dm" $ROOTDIR/examples/heat/heat -ntheta 66 -nthick 130 -nblocks 8 -v2 -pin 2> /dev/null`
save_cov "heat.dm.8k.calibrate.v2";


echo "heat.dm.16k.calibrate.v2"
timing=`CALIBRATE=1 SCHED="dm" $ROOTDIR/examples/heat/heat -ntheta 130 -nthick 130 -nblocks 16 -v2 -pin 2> /dev/null`
save_cov "heat.dm.16k.calibrate.v2";



echo "heat.dm.8k.no.pin.v2"
timing=`SCHED="dm" $ROOTDIR/examples/heat/heat -ntheta 66 -nthick 130 -nblocks 8 -v2 2> /dev/null`
save_cov "heat.dm.8k.no.pin.v2";

echo "heat.prio.8k"
timing=`SCHED="prio" $ROOTDIR/examples/heat/heat -ntheta 66 -nthick 130 -nblocks 8 -v2 -pin 2> /dev/null`
save_cov "heat.prio.8k";

echo "heat.dm.8k.v2.no.prio"
timing=`SCHED="no-prio" $ROOTDIR/examples/heat/heat -ntheta 66 -nthick 130 -nblocks 8 -pin -v2 2> /dev/null`
save_cov "heat.dm.8k.v2.no.prio";

echo "heat.dm.8k.v2.random"
timing=`SCHED="random" $ROOTDIR/examples/heat/heat -ntheta 66 -nthick 130 -nblocks 8 -pin -v2 2> /dev/null`
save_cov "heat.dm.8k.v2.random";

echo "heat.dm.8k.v2"
timing=`SCHED="dm" $ROOTDIR/examples/heat/heat -ntheta 66 -nthick 130 -nblocks 8 -pin -v2 2> /dev/null`
save_cov "heat.dm.8k.v2";

echo "heat.dm.16k.v2"
timing=`SCHED="dm" $ROOTDIR/examples/heat/heat -ntheta 130 -nthick 130 -nblocks 16 -pin -v2 2> /dev/null`
save_cov "heat.dm.16k.v2";

echo "heat.ws.8k.v2"
timing=`SCHED="ws" $ROOTDIR/examples/heat/heat -ntheta 66 -nthick 130 -nblocks 8 -pin -v2 2> /dev/null`
save_cov "heat.ws.8k.v2";

echo "heat.greedy.8k.v2"
timing=`SCHED="greedy" $ROOTDIR/examples/heat/heat -ntheta 66 -nthick 130 -nblocks 8 -pin -v2 2> /dev/null`
save_cov "heat.greedy.8k.v2";

echo "heat.dm.8k.cg"
timing=`$ROOTDIR/examples/heat/heat -ntheta 66 -nthick 130 -nblocks 8 -pin -v2 -cg 2> /dev/null`
save_cov "heat.dm.8k.cg";

echo "heat.dm.8k.v3"
timing=`SCHED="dm" $ROOTDIR/examples/heat/heat -ntheta 66 -nthick 130 -nblocks 8 -pin -v3 2> /dev/null`
save_cov "heat.dm.8k.v3";

generatehtml;
