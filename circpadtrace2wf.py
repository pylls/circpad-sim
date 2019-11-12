#!/usr/bin/env python3
import argparse
import sys
import os
import common

ap = argparse.ArgumentParser()
ap.add_argument("-i", required=True,
    help="input folder of client circpadtraces to transform")
ap.add_argument("-o", required=True,
    help="output folder for storing resulting WF files")
ap.add_argument("-t", required=True,
    help="the out type, one of: cells, timecells, dirtime")

ap.add_argument("-r", required=False,
    help="input folder of relay circpadtraces, will change timestamp calculations")
ap.add_argument("-f", required=False,
    help="override output filetype (.cells, .timecells, .dirtime, .pkl)")
ap.add_argument("-p", required=False, type=bool,
    help="don't store results in output, but in one pickle file")

# FIXME: consider having a noise parameter to randomize the environment

# FIXME: we have to randomize the client traces when producing the WF output,
# because otherwise we run the risk of having time-based classifiers getting too
# accurate timings from the client ("+mikeperry | pulls: if the framework allows
# this as the default/easiest way to use it, that is what attack papers will
# use.. and then they will conclude these attacks are indefensible because
# (surprise) deep learning is able to classify pages based on CPU and memory
# usage timings on a local machine (no surprise, really)")
args = vars(ap.parse_args())

def main():
    '''Given an input folder of client circpadtrace files generated by 
    torlog2circpadtrace.py, transforms them to different formats suitable for
    Website Fingerprinting (WF) classifiers. 

    Supported output formats (one of): 
    - Cells, each line only contains 1 or -1 for outgoing or incoming cells.
    - TimeCells, relative timestamp (ms) added before each cell.
    - DirectionalTime, each line has relative time multiplied with cell value.

    The Cells format is the most commonly used, while TimeCells adds time, and
    DirectionalTime is the format used by Rahman et al. in Tik-Tok.

    Timestamps are taken directly from the client's trace. This is way too
    optimistic for an attacker, because it's much closer to Tor Browser's
    (Firefox's) internal processing than any network attacker would get. Because
    of this, by default, we add some random noise to the timestamps. We strongly
    encourage you to think twice about disabling this, results will likely be
    too optimistic if your attack uses time.

    TODO: If a relay folder is provided with the corresponding relay traces, the
    timestamp calculation is changed to as advantageous as we can make them to
    the attacker. This will be stronger than the typical WF attacker that only
    observes network traffic at one location, e.g., at a guard or middle relay,
    or at client's ISP. For cells sent, 
    '''
    if not os.path.isdir(args["i"]):
        sys.exit(f"{args['i']} is not a directory")
    if not os.path.isdir(args["o"]):
        sys.exit(f"{args['o']} is not a directory")

    WF_TYPES = [
        "cells",
        "timecells",
        "dirtime"
    ]

    if not any(t in args["t"] for t in WF_TYPES):
        sys.exit(f"invalid type, has to be one of {WF_TYPES}")

    for fname in os.listdir(args["i"]):
        infname = os.path.join(args["i"], fname)
        outfname = os.path.join(args["o"], fname)

        if os.path.exists(outfname):
            sys.exit(f"output file {outfname} already exists")
        
        wf_out = []
        with open(infname, 'r') as f:
            trace = common.circpad_lines_to_trace(f.readlines())
            if args["t"] == "cells":
                wf_out = common.circpad_wf_cells(trace)
            elif args["t"] == "timecells":
                wf_out = common.circpad_wf_timecells(trace)
            elif args["t"] == "dirtime":
                wf_out = common.circpad_wf_dirtime(trace)

        with open(outfname, 'w') as f:
            for l in wf_out:
                f.write(f"{l}\n")
        
if __name__ == "__main__":
    main()