#!/usr/bin/env python3
import argparse
import common
import sys
import os

ap = argparse.ArgumentParser()
ap.add_argument("-i", required=True, 
    help="input folder of circpadtrace files")

# FIXME 
ap.add_argument("-o", required=False,
    help="output file for results (JSON)")
args = vars(ap.parse_args())

# TODO: compare folder for estimating time overhead

def main():
    '''Given an input folder of circpadtrace files generated by 
    torlog2circpadtrace.py, computes overhead statistics.

    Bandwidth overhead is based on number of padding and non-padding cells in
    all traces. If a comparison folder of traces is given (-c argument), also
    computes statistics on relative time overheads. For time overheads to be
    reasonable, make sure the folders of traces represent the same thing (that
    is, visits to the same websites during approximately the same time and the
    same number of visits). 
    '''

    if not os.path.isdir(args["i"]):
        sys.exit(f"{args['i']} is not a directory")

    bw_sent_total = []
    bw_recv_total = []
    bw_sent_overhead = []
    bw_recv_overhead = []
    for fname in os.listdir(args["i"]):
        infname = os.path.join(args["i"], fname)

        with open(infname, 'r') as f:
            trace = f.readlines()

            sent_nonpadding, recv_nonpadding = common.circpad_get_nonpadding_times(trace)
            sent_padding, recv_padding = common.circpad_get_padding_times(trace)

            if len(sent_nonpadding) == 0:
                sys.exit(f"{infname} sent 0 nonpadding cells, broken trace?")
            if len(recv_nonpadding) == 0:
                sys.exit(f"{infname} recv 0 nonpadding cells, broken trace?")

            total_sent = len(sent_padding + sent_nonpadding)
            total_recv = len(recv_padding + recv_nonpadding)
            bw_sent_total.append(total_sent)
            bw_recv_total.append(total_recv)
            bw_sent_overhead.append(float(total_sent) / float(len(sent_nonpadding)))
            bw_recv_overhead.append(float(total_recv) / float(len(recv_nonpadding)))

    total_sent = sum(bw_sent_total) + sum(bw_recv_total)
    print(
        f"for {len(bw_sent_overhead)} traces, {total_sent} cells in total, "
        f"{sum(bw_sent_total)} sent "
        f"({float(sum(bw_sent_total))/float(total_sent):.0%}) and "
        f"{sum(bw_recv_total)} recv "
        f"({float(sum(bw_recv_total))/float(total_sent):.0%})"
    )

    avg_sent = float(sum(bw_sent_overhead)/float(len(bw_sent_overhead)))
    avg_recv = float(sum(bw_recv_overhead)/float(len(bw_recv_overhead)))
    avg_total = float(sum(bw_sent_overhead) + sum(bw_recv_overhead))/float(len(bw_sent_overhead) + len(bw_recv_overhead))
    print(f"\t- average sent bandwidth:\t{avg_sent:.0%}")
    print(f"\t- average recv bandwidth:\t{avg_recv:.0%}")
    print(f"\t- average total bandwidth:\t{avg_total:.0%}")

if __name__ == "__main__":
    main()