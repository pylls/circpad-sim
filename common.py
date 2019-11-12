#!/usr/bin/env python3
import sys
import socket

CIRCPAD_ERROR_WRONG_FORMAT = "invalid trace format"
CIRCPAD_ADDRESS_EVENT = "connection_ap_handshake_send_begin"

CIRCPAD_EVENT_NONPADDING_SENT = "circpad_cell_event_nonpadding_sent"
CIRCPAD_EVENT_NONPADDING_RECV = "circpad_cell_event_nonpadding_received"
CIRCPAD_EVENT_PADDING_SENT = "circpad_cell_event_padding_sent"
CIRCPAD_EVENT_PADDING_RECV = "circpad_cell_event_padding_received"

CIRCPAD_LOG = "circpad_trace_event"
CIRCPAD_LOG_TIMESTAMP = "timestamp="
CIRCPAD_LOG_CIRC_ID = "client_circ_id="
CIRCPAD_LOG_EVENT = "event="

CIRCPAD_BLACKLISTED_ADDRESSES = ["aus1.torproject.org"]
CIRCPAD_BLACKLISTED_EVENTS = [
    "circpad_negotiate_padding", 
    "circpad_handle_padding_negotiated", 
    "circpad_handle_padding_negotiate",
    "circpad_padding_negotiated"
]

def circpad_get_all_addresses(trace):
    addresses = []
    for l in trace:
        if len(l) < 2:
            sys.exit(CIRCPAD_ERROR_WRONG_FORMAT)
        if CIRCPAD_ADDRESS_EVENT in l[1]:
            if len(l[1]) < 2:
                sys.exit(CIRCPAD_ERROR_WRONG_FORMAT)
            addresses.append(l[1].split()[1])
    return addresses

def circpad_get_nonpadding_times(trace):
    sent_nonpadding, recv_nonpadding = [], []

    for l in trace:
        split = l.split()
        if CIRCPAD_EVENT_NONPADDING_SENT in split[1]:
            sent_nonpadding.append(split[0])
        elif CIRCPAD_EVENT_NONPADDING_RECV in split[1]:
            recv_nonpadding.append(split[0])
    
    return sent_nonpadding, recv_nonpadding

def circpad_get_padding_times(trace):
    sent_padding, recv_padding = [], []

    for l in trace:
        split = l.split()
        if CIRCPAD_EVENT_PADDING_SENT in split[1]:
            sent_padding.append(split[0])
        elif CIRCPAD_EVENT_PADDING_RECV in split[1]:
            recv_padding.append(split[0])
    
    return sent_padding, recv_padding

def circpad_parse_line(line):
    split = line.split()
    assert(len(split) >= 2)
    event = split[1]
    timestamp = int(split[0])

    return event, timestamp

def circpad_extract_log_traces(
    log_lines,
    source_client=True,
    source_relay=True,
    allow_ips=False,
    filter_client_negotiate=False,
    filter_relay_negotiate=False
    ):
    # helper function
    def blacklist_hit(d):
        for a in circpad_get_all_addresses(d):
            if a in CIRCPAD_BLACKLISTED_ADDRESSES:
                return True
        return False

    # helper to extract one line
    def extract_from_line(line):
        n = line.index(CIRCPAD_LOG_TIMESTAMP)+len(CIRCPAD_LOG_TIMESTAMP)
        timestamp = line[n:].split(" ", maxsplit=1)[0]
        n = line.index(CIRCPAD_LOG_CIRC_ID)+len(CIRCPAD_LOG_CIRC_ID)
        cid = line[n:].split(" ", maxsplit=1)[0]

        # an event is the last part, no need to split on space like we did earlier
        n = line.index(CIRCPAD_LOG_EVENT)+len(CIRCPAD_LOG_EVENT)
        event = line[n:]
        
        return cid, int(timestamp), event

    circuits = {}
    base = -1
    for line in log_lines:
        if CIRCPAD_LOG in line:
            # skip client/relay if they shouldn't be part of the trace
            if not source_client and "source=client" in line:
                continue
            if not source_relay and "source=relay" in line:
                continue

            # extract trace and make timestamps relative
            cid, timestamp, event = extract_from_line(line)
            if base == -1:
                base = timestamp
            timestamp = timestamp - base

            # store trace
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
        if not allow_ips and circpad_only_ips_in_trace(circuits[cid]):
            del circuits[cid]

    # remove blacklisted events (and associated events)
    for cid in list(circuits.keys()):
        circuits[cid] = circpad_remove_blacklisted_events(circuits[cid],
                        filter_client_negotiate, filter_relay_negotiate)
    
    return circuits
    

def circpad_remove_blacklisted_events(
    trace, 
    filter_client_negotiate, 
    filter_relay_negotiate
    ):
    
    # three cases: no argument, filter client, or filter relay
    result = []
    ignore_next_events = 0

    for line in trace:
        # we always filter blacklisted events
        if not any(b in line for b in CIRCPAD_BLACKLISTED_EVENTS):
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
    if filter_client_negotiate and "circpad_negotiate_padding" in line:
        ignore_next_events = 2

    '''
    For the relay, we assume a list as followes:
      1. circpad_handle_padding_negotiate
      2. circpad_padding_negotiated
      3. circpad_cell_event_nonpadding_sent
    On observing (blacklisted) event 2, we ignore the following 1 event
    '''
    if filter_relay_negotiate and "circpad_padding_negotiated" in line:
        ignore_next_events = 1

    return result

def circpad_only_ips_in_trace(trace):
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

    for a in circpad_get_all_addresses(trace):
        if not is_ipv4(a) and not is_ipv6(a):
            return False
    return True