#!/usr/bin/env python3
import argparse
import os
import sys

import common

ap = argparse.ArgumentParser()
ap.add_argument("-i", required=True, 
    help="input folder of torlog files to transform")
ap.add_argument("-o", required=True, 
    help="output folder to store circpadtrace files in")

ap.add_argument('-c', default=True, action='store_true',
    help="include trace events with source client")
ap.add_argument('-r', default=True, action='store_true',
    help="include trace events with source relay")
ap.add_argument('--ip', default=False, action='store_true',
    help="don't filter out circuits with only ipv4 or ipv6 addresses")
ap.add_argument('--fnc', default=False, action='store_true',
    help="filter out all highly-likely client-side negotiate events")
ap.add_argument('--fnr', default=False, action='store_true',
    help="filter out all highly-likely relay-side negotiate events")

args = vars(ap.parse_args())

CONST_OTHER_MAX_EVENTS_WARNING = 100

def main():
    '''Given an input folder of logs from instrumented tor to log circuitpad traces 
    to its global log, transforms the logs circpadtrace files. 

    The script will determine the circuit with the most circpad events in the
    log and extract them. A warning is written to stdout if any _other_ circuit
    has more than CONST_OTHER_MAX_EVENTS_WARNING events as well in the log.
    '''

    if not os.path.isdir(args["i"]):
        sys.exit(f"{args['i']} is not a directory")
    if not os.path.isdir(args["o"]):
        sys.exit(f"{args['o']} is not a directory")

    for fname in os.listdir(args["i"]):
        infname = os.path.join(args["i"], fname)
        outfname = os.path.join(args["o"], fname)

        if os.path.exists(outfname):
            sys.exit(f"output file {outfname} already exists")

        # create dictionary with circuit_id -> [(timestamp, event)]
        circuits = {}
        with open(infname, 'r') as f:
            circuits = common.circpad_extract_log_traces(f.readlines(),
                args["c"], args["r"], args["ip"], args["fnc"], args["fnr"])

        if len(circuits) == 0:
            sys.exit(f"no valid circuits found for {infname}")

        # figure out which circuit has the most events, keeping tabs of the rest
        longest_cid = -1
        longest_cid_len = -1
        smaller_cids = []
        for cid in circuits:
            if len(circuits[cid]) > longest_cid_len:
                if longest_cid != -1:
                    smaller_cids.append(longest_cid)
                longest_cid = cid
                longest_cid_len = len(circuits[cid])

        # all done, time to save results
        # we make the time relative here from the selected circuit, in common
        # it's relative to the first event in the first circuit
        base = -1
        with open(outfname, 'w') as f:
            for l in circuits[longest_cid]:
                t = int(l[0])
                if base == -1:
                    base = t
                t = t - base
                f.write(f"{t:016d} {l[1].strip()}\n")
        
        for cid in smaller_cids:
            if len(circuits[cid]) > CONST_OTHER_MAX_EVENTS_WARNING:
                print(f"warning: found extra circuit with {len(circuits[cid])} events in {infname}")

if __name__ == "__main__":
    main()