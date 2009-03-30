#!/bin/sh

TEST_DIR=$PWD

ntests=0
nfailed=0
nsucess=0

print_summary() {
	if test $nfailed = 0; then
		echo "**** All tests are successful ****"
	else
		echo "$nfailed test(s) failed out of $ntests" 
	fi
}

test_with_timeout() {
	timeout=$1
        application=$2

	ntests=$(($ntests + 1))

	echo "$application"

	$application > /dev/null 2> /dev/null & _pid_appli=$!;
	"(sleep $timeout ; kill -9 $_pid_appli 2> /dev/null)" & _pid_killer=$!
	wait $_pid_appli
	ret=$?
	kill $_pid_killer 2> /dev/null
	if test $ret = 0; then
		echo "	SUCCESS"
		nsuccess=$(($nsuccess + 1))
	else
		case $ret in
			137) # sigkill
			echo "	TEST TIMEOUT"
			;;
			139)
			echo "	TEST FAILED: SIGSEV"
			;;
			*)
			echo "	TEST FAILED (ret = $ret)"
		esac
		nfailed=$(($nfailed + 1))
	fi
}

make clean > /dev/null 2> /dev/null
make > /dev/null 2> /dev/null
if test $? != 0; then
	echo "Compilation failed, check installation ..."
	exit 1
fi

echo
echo "**********************"
echo "TEST synchronous tasks"
echo "**********************"
echo

test_with_timeout 10 "./sync-tasks-overhead -i 10000" 2> /dev/null 

echo
echo "***********************"
echo "TEST asynchronous tasks"
echo "***********************"
echo

test_with_timeout 10 "./async-tasks-overhead -i 20000" 2> /dev/null 

echo
echo "**************"
echo "TEST increment"
echo "**************"
echo

test_with_timeout 10 "../../examples/incrementer/incrementer" 2> /dev/null 

echo
echo "**********"
echo "TEST tag 1"
echo "**********"
echo

test_with_timeout 10 "../../examples/tag_example/tag_example -iter 1000" 2> /dev/null 

echo
echo "**********"
echo "TEST tag 2"
echo "**********"
echo

test_with_timeout 10 "../../examples/tag_example/tag_example2 -iter 100" 2> /dev/null 



echo
echo "*******"
echo "SUMMARY"
echo "*******"
echo

print_summary
