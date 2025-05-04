#!/bin/bash

# Path to the ARM qmake executable
QT_ARM_QMAKE="/home/lyp/Codes/I.MX6ULL/utils/arm-qt/bin/qmake"

# Full path to the default system qmake executable
QT_DEFAULT_QMAKE="/usr/bin/qmake"

# Variable to hold the currently selected qmake executable path
SELECTED_QMAKE="$QT_DEFAULT_QMAKE" # Default to system qmake

# Check the first command-line argument
if [ "$1" == "--arm" ]; then
    echo "Using ARM Qt build."
    SELECTED_QMAKE="$QT_ARM_QMAKE"
fi

echo "Using $SELECTED_QMAKE"

# Run qmake
"$SELECTED_QMAKE" || { echo "qmake failed."; exit 1; }

# Run make qmake_all
make -f ./Makefile qmake_all || { echo "make qmake_all failed."; exit 1; }

# Run final make
make || { echo "make failed."; exit 1; }

make clean && rm Makefile .qmake.stash

exit 0
