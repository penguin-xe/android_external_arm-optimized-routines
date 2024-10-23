#!/bin/bash

# ULP error check script.
#
# Copyright (c) 2019-2024, Arm Limited.
# SPDX-License-Identifier: MIT OR Apache-2.0 WITH LLVM-exception

#set -x
set -eu

# cd to bin directory.
cd "${0%/*}"

flags="${ULPFLAGS:--q}"
emu="$@"

# Enable SVE testing
WANT_SVE_MATH=${WANT_SVE_MATH:-0}

FAIL=0
PASS=0

t() {
    # First argument: routine name
    routine=$1; shift
    # Second and third argument: lo and hi bounds
    # Extra processing needed for bivariate routines
    IFS=',' read -ra LO <<< "$1"; shift
    IFS=',' read -ra HI <<< "$1"; shift
    ITV="${LO[0]} ${HI[0]}"
    for i in "${!LO[@]}"; do
	[[ "$i" -eq "0" ]] || ITV="$ITV x ${LO[$i]} ${HI[$i]}"
    done
    # Fourth argument: number of test points
    n=$1; shift
    # Any remaining arguments forwards directly to ulp tool
    extra_flags="$@"

    # Read ULP limits and fenv expectation from autogenerated files (no check for non-nearest limits file)
    L=$(grep "^$routine " $LIMITS | awk '{print $2}')
    [ -n "$L" ] || { echo ERROR: Could not determine ULP limit for $routine in $LIMITS && false; }
    if grep -q "^$routine$" $FENV; then extra_flags="$extra_flags -f"; fi 

    # Add -z flag to ignore zero sign for vector routines - note the version of this script in the main
    # math directory passes this for every routine in ARCH directory
    grep -q "ZGV" <<< "$routine" && extra_flags="$extra_flags -z"

    # Run ULP tool - math/ version of this script passes rounding mode, but PL routines only support
    # round-to-nerest
    $emu ./ulp -e $L $flags $extra_flags $routine $ITV $n && PASS=$((PASS+1)) || FAIL=$((FAIL+1))
}

check() {
	$emu ./ulp -f -q "$@" #>/dev/null
}

if [ "$FUNC" == "atan2" ] || [ -z "$FUNC" ]; then
    # Regression-test for correct NaN handling in atan2
    check atan2 0x1p-1022 0x1p-1000 x 0 0x1p-1022 40000
    check atan2 0x1.7887a0a717aefp+1017 0x1.7887a0a717aefp+1017 x -nan -nan
    check atan2 nan nan x -nan -nan
fi

# vector functions
flags="${ULPFLAGS:--q}"
runsv=
if [ $WANT_SVE_MATH -eq 1 ] && [[ $USE_MPFR -eq 0 ]]; then
# No guarantees about powi accuracy, so regression-test for exactness
# w.r.t. the custom reference impl in ulp_wrappers.h
    if [ -z "$FUNC" ] || [ "$FUNC" == "_ZGVsMxvv_powi" ]; then
	check -q -f -e 0 _ZGVsMxvv_powi  0  inf x  0  1000 100000 && runsv=1
	check -q -f -e 0 _ZGVsMxvv_powi -0 -inf x  0  1000 100000 && runsv=1
	check -q -f -e 0 _ZGVsMxvv_powi  0  inf x -0 -1000 100000 && runsv=1
	check -q -f -e 0 _ZGVsMxvv_powi -0 -inf x -0 -1000 100000 && runsv=1
    fi
    if [ -z "$FUNC" ] || [ "$FUNC" == "_ZGVsMxvv_powk" ]; then
	check -q -f -e 0 _ZGVsMxvv_powk  0  inf x  0  1000 100000 && runsv=1
	check -q -f -e 0 _ZGVsMxvv_powk -0 -inf x  0  1000 100000 && runsv=1
	check -q -f -e 0 _ZGVsMxvv_powk  0  inf x -0 -1000 100000 && runsv=1
	check -q -f -e 0 _ZGVsMxvv_powk -0 -inf x -0 -1000 100000 && runsv=1
    fi
fi

while read F LO HI N C
do
        [[ -z "$C" ]] || C="-c $C"
	[[ -z $F ]] || t $F $LO $HI $N $C
done << EOF
$(cat $INTERVALS | grep "\b$FUNC\b")
EOF

[ 0 -eq $FAIL ] || {
	echo "FAILED $FAIL PASSED $PASS"
	exit 1
}
