#!/bin/bash
exec adb -s ${FOLD:-SERIAL_DEL_DISPOSITIVO} "$@"
