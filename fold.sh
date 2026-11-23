#!/bin/bash
exec adb -s ${FOLD:-} "$@"
