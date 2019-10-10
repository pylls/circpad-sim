#!/usr/bin/env python3
import argparse
import os
import sys
import socket
import common

ap = argparse.ArgumentParser()
ap.add_argument("-i", required=True, 
    help="input folder of torlog files to transform")
ap.add_argument("-o", required=True, 
    help="output folder to store circpadtrace files in")

ap.add_argument('--ip', default=False, action='store_true',
    help="don't filter out circuits with only ipv4 or ipv6 addresses")
args = vars(ap.parse_args())

CONST_OTHER_MAX_EVENTS_WARNING = 100
CONST_CIRCPAD_EVENT_STR = "circpad_event_callback():"

blacklisted_addresses = ["aus1.torproject.org"]

def main():
    '''Given an input folder of logs from instrumented tor to log circuitpad traces to its global log, transforms the logs circpadtrace files. 

    The script will determine the circuit with the most circpad events in the
    log and extract them. A warning is written to stdout if any _other_ circuit
    has more than CONST_OTHER_MAX_EVENTS_WARNING events as well in the log.
    '''

    if args["ip"]:
        sys.exit("yay")

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
            for line in f:
                if CONST_CIRCPAD_EVENT_STR in line:
                    cid, timestamp, event = extract_callback(line)
                    if cid in circuits.keys():
                        circuits[cid] = circuits.get(cid) + [(timestamp, event)]
                    else:
                        circuits[cid] = [(timestamp, event)]

        # filter out circuits with blacklisted addresses
        for cid in list(circuits.keys()):
            if blacklist_hit(circuits[cid]):
                del circuits[cid]
        # filter out circuits with only IPs (unless arg says otherwise)
        for cid in list(circuits.keys()):
            if not args["ip"] and only_ips(circuits[cid]):
                del circuits[cid]

        if len(circuits) == 0:
            sys.exit(f"no circuits left after filtering for {infname}")

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

        # all done, time to save results and warn 
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

def extract_callback(line):
    parts = line.split()

    # find where the event line starts, log format (prefix) of tor could change
    start = -1
    for i, p in enumerate(parts):
        if p == CONST_CIRCPAD_EVENT_STR:
            start = i
            break  
    if start == -1:
        sys.exit("bug: malformed circpad line")
    
    timestamp = parts[start+1]
    cid = parts[start+2]
    # an event can have one or more parts
    event = ""
    for p in parts[start+3:]:
        event += " " + p

    return cid, timestamp, event

def blacklist_hit(d):
    for a in common.circpad_get_all_addresses(d):
        if a in blacklisted_addresses:
            return True
    return False

def only_ips(d):
    def is_ipv4(addr):
        try:
            socket.inet_aton(addr)
        except (socket.error, TypeError):
            return False
        return True
    def is_ipv6(addr):
        try:
            socket.inet_pton(addr,socket.AF_INET6)
        except (socket.error, TypeError):
            return False
        return True

    for a in common.circpad_get_all_addresses(d):
        if not is_ipv4(a) and not is_ipv6(a):
            return False
    return True


if __name__ == "__main__":
    main()