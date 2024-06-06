#!/bin/sh

./autogen.sh || exit 1
./configure || exit 1
exec make
