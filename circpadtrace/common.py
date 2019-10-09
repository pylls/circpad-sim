#!/usr/bin/env python3
import sys

CIRCPAD_ERROR_WRONG_FORMAT = "invalid trace format"
CIRCPAD_ADDRESS_EVENT = "connection_ap_handshake_send_begin"

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