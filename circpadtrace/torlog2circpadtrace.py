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
ap.add_argument('--fnc', default=False, action='store_true',
    help="filter out all highly-likely client-side negotiate events")
ap.add_argument('--fnr', default=False, action='store_true',
    help="filter out all highly-likely relay-side negotiate events")

args = vars(ap.parse_args())

CONST_OTHER_MAX_EVENTS_WARNING = 100
CONST_CIRCPAD_TRACE = "circpad_trace_event"
CONST_CIRCPAD_TRACE_TIMESTAMP = "timestamp="
CONST_CIRCPAD_TRACE_CIRC_ID = "client_circ_id="
CONST_CIRCPAD_TRACE_EVENT = "event="

blacklisted_addresses = ["aus1.torproject.org"]
blacklisted_events = [
    "circpad_negotiate_padding", 
    "circpad_handle_padding_negotiated", 
    "circpad_handle_padding_negotiate",
    "circpad_padding_negotiated"
]

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
            for line in f:
                cid, timestamp, event, ok = extract_trace(line)
                if not ok:
                    continue
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

        # remove blacklisted events (and associated events)
        for cid in list(circuits.keys()):
            circuits[cid] = remove_blacklisted_events(circuits[cid])

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

def remove_blacklisted_events(trace):
    # three cases: no argument, filter client, or filter relay
    result = []
    ignore_next_events = 0

    for line in trace:
        # we always filter blacklisted events
        if not any(b in line for b in blacklisted_events):
            if ignore_next_events == 0:
                result.append(line)
            else:
                ignore_next_events -= 1

    '''
    For the client, we assume a list as follows:
      1. circpad_negotiate_padding
      2. circpad_cell_event_nonpadding_sent
      3. circpad_cell_event_nonpadding_received
      4. circpad_handle_padding_negotiated
    On observing (blacklisted) event 1, we ignore the following 2 events
    '''
    if args["fnc"] and "circpad_negotiate_padding" in line:
        ignore_next_events = 2

    '''
    For the relay, we assume a list as followes:
      1. circpad_handle_padding_negotiate
      2. circpad_padding_negotiated
      3. circpad_cell_event_nonpadding_sent
    On observing (blacklisted) event 2, we ignore the following 1 event
    '''
    if args["fnr"] and "circpad_padding_negotiated" in line:
        ignore_next_events = 1

    return result

def extract_trace(line):
    if CONST_CIRCPAD_TRACE not in line:
        return False, False, False, False

    for b in blacklisted_events:
        if b in line:
            return False, False, False, False

    n = line.index(CONST_CIRCPAD_TRACE_TIMESTAMP)+len(CONST_CIRCPAD_TRACE_TIMESTAMP)
    timestamp = line[n:].split(" ", maxsplit=1)[0]
    n = line.index(CONST_CIRCPAD_TRACE_CIRC_ID)+len(CONST_CIRCPAD_TRACE_CIRC_ID)
    cid = line[n:].split(" ", maxsplit=1)[0]

    # an event is the last part, no need to split on space like we did earlier
    n = line.index(CONST_CIRCPAD_TRACE_EVENT)+len(CONST_CIRCPAD_TRACE_EVENT)
    event = line[n:]
    
    return cid, timestamp, event, True

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