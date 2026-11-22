#!/bin/bash
cd /home/mfw/Documents/claude001/NewOS
make -j4 2>&1 | tail -5
echo "BUILD EXIT: $?"
python3 tools/test-control.py 2>&1 | tail -30
