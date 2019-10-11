#!/usr/bin/env python3
import sys

CIRCPAD_ERROR_WRONG_FORMAT = "invalid trace format"
CIRCPAD_ADDRESS_EVENT = "connection_ap_handshake_send_begin"

CIRCPAD_EVENT_NONPADDING_SENT = "circpad_cell_event_nonpadding_sent"
CIRCPAD_EVENT_NONPADDING_RECV = "circpad_cell_event_nonpadding_received"
CIRCPAD_EVENT_PADDING_SENT = "circpad_cell_event_padding_sent"
CIRCPAD_EVENT_PADDING_RECV = "circpad_cell_event_padding_received"

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
    timestamp = 123

    return event, timestamp