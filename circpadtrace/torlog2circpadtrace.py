#!/usr/bin/env python3
import argparse
import common

ap = argparse.ArgumentParser()
ap.add_argument("-i", required=True, 
    help="input folder of torlog files to transform")
ap.add_argument("-o", required=True, 
    help="output folder to store circpadtrace files in")
args = vars(ap.parse_args())

CONST_OTHER_MAX_CELLS_WARNING = 100

blacklisted_addresses = ["torproject.org"]

def main():
    '''Given an input folder of logs from instrumented tor to log circuitpad traces to its global log, transforms the logs circpadtrace files. 

    The script will determine the circuit that sent the most nonpadding cells in
    the log and ext ract that circuits circpad events. A warning is written to
    stdout if any _other_ circuit has more than CONST_OTHER_MAX_CELLS_WARNING
    nonpadding cells sent.
    '''

    print("hello world")

if __name__ == "__main__":
    main()