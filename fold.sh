#!/bin/bash
exec adb ${FOLD:+-s $FOLD} "$@"
