#!/bin/bash -e

VERBOSE=1 make test

tests=$(find bin/test/ -executable -type f)

for i in $tests; do
    echo $i:
    echo ---------
    ./$i || exit 1;
    echo;
done

test/driver.sh
