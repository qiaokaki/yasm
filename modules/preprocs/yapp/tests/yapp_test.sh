#! /bin/sh
# $Id$

case `echo "testing\c"; echo 1,2,3`,`echo -n testing; echo 1,2,3` in
  *c*,-n*) ECHO_N= ECHO_C='
' ECHO_T='	' ;;
  *c*,*  ) ECHO_N=-n ECHO_C= ECHO_T= ;;
  *)       ECHO_N= ECHO_C='\c' ECHO_T= ;;
esac

mkdir results >/dev/null 2>&1

#
# Verify that all test cases match
# (aside from whitespace for now)
#

passedct=0
failedct=0

YT="yapp_test"


echo $ECHO_N "Test $YT: "
for asm in ${srcdir}/modules/preprocs/yapp/tests/*.asm
do
    a=`echo ${asm} | sed 's,^.*/,,;s,.asm$,,'`
    o=${a}.yp
    og=`echo ${asm} | sed 's,.asm$,.pre,'`
    e=${a}.ew
    eg=`echo ${asm} | sed 's,.asm$,.errwarn,'`

    # Run within a subshell to prevent signal messages from displaying.
    sh -c "sed \"s,\./,${srcdir}/,\" ${asm} | ./yasm -e -r yapp | sed \"s,${srcdir}/,./,\" > results/${o} 2>results/${e}" >/dev/null 2>/dev/null
    status=$?
    if test $status -gt 128; then
	# We should never get a coredump!
	echo $ECHO_N "C"
        eval "failed$failedct='C: ${a} crashed!'"
	failedct=`expr $failedct + 1`
    elif test $status -gt 0; then
	echo ${asm} | grep err >/dev/null
       	if test $? -gt 0; then
	    # YASM detected errors but shouldn't have!
	    echo $ECHO_N "E"
            eval "failed$failedct='E: ${a} returned an error code!'"
	    failedct=`expr $failedct + 1`
	else
	    # We got errors, check to see if they match:
	    if (test \! -e ${eg} && test -s results/${e}) ||
	       (test -e ${eg} && diff ${eg} results/${e} >/dev/null); then
		# Error/warnings don't match.
		echo $ECHO_N "W"
                eval "failed$failedct='W: ${a} did not match errors and warnings!'"
		failedct=`expr $failedct + 1`
	    else
		# Error/warnings match, it passes!
		echo $ECHO_N "."
		passedct=`expr $passedct + 1`
	    fi
	fi
    else
	echo ${asm} | grep -v err >/dev/null
       	if test $? -gt 0; then
	    # YASM detected errors but shouldn't have!
	    echo $ECHO_N "E"
            eval "failed$failedct='E: ${a} returned an error code!'"
	    failedct=`expr $failedct + 1`
	else
	    diff -w ${og} results/${o} > /dev/null
	    if test $? -eq 0; then
		if (test \! -e ${eg} && test -s results/${e}) ||
		   (test -e ${eg} && diff ${eg} results/${e} >/dev/null); then
		    # Error/warnings don't match.
		    echo $ECHO_N "W"
                    eval "failed$failedct='W: ${a} did not match errors and warnings!'"
		    failedct=`expr $failedct + 1`
		else
		    # Both output file and error/warnings match, it passes!
		    echo $ECHO_N "."
		    passedct=`expr $passedct + 1`
		fi
	    else
		# Output file doesn't match.
		echo $ECHO_N "O"
                eval "failed$failedct='O: ${a} did not match output file!'"
		failedct=`expr $failedct + 1`
	    fi
	fi
    fi
done

ct=`expr $failedct + $passedct`
per=`expr 100 \* $passedct / $ct`

echo " +$passedct-$failedct/$ct $per%"
i=0
while test $i -lt $failedct; do
    eval "failure=\$failed$i"
    echo " ** $failure"
    i=`expr $i + 1`
done

exit $failedct
