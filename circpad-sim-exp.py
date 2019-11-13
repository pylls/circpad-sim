#!/usr/bin/env python3
import argparse
import sys
import os
import shutil
import subprocess
import tempfile
import common

ap = argparse.ArgumentParser()
ap.add_argument("-c", required=True, 
    help="input folder of client circpadtrace files")
ap.add_argument("-r", required=True, 
    help="input folder of relay circpadtrace files")
ap.add_argument("-t", required=True, 
    help="path to tor folder (bob/tor, not bob/tor/src)")

args = vars(ap.parse_args())

TOR_CIRCPADSIM_SRC_LOC = "src/test/test_circuitpadding_sim.c"
CLIENT_MACHINE_TOKEN = "//REPLACE-client-padding-machine-REPLACE"
RELAY_MACHINE_TOKEN = "//REPLACE-relay-padding-machine-REPLACE"
TOR_CIRCPADSIM_CMD = os.path.join(args["t"], "src/test/test circuitpadding_sim/..")
TOR_CIRCPADSIM_CMD_FORMAT = f"{TOR_CIRCPADSIM_CMD} --info --circpadsim {{}} {{}} 1"

circpadsim_src = "placeholder"
circpad_sim_loc = os.path.join(args["t"], TOR_CIRCPADSIM_SRC_LOC)

tmpdir = tempfile.mkdtemp()

def main():
    '''A brief example with mostly comments of how one could evaluate padding 
    machines with the circuitpadding simulator.

    View this as a first outline in absence of more complete evaluators
    hopefully done by researchers in the near future. We only outline the
    general approach and implement the parts that relate directly with how the
    simulator works. The rest is future work. 

    An evaluator could work as follows:
    - read test_circuitpadding_sim.c, copy in machines, save, make
    - simulate machines, parse simulated traces
    - transform traces to WF traces
    - evaluate machines on attacks
    - learn from the evaluation, build better machines, repeat

    This script implements the first three steps.
    '''
    # list of input traces, sorted assuming the matching client and relay traces
    # have the same name in respective folders
    if not os.path.isdir(args["c"]):
        sys.exit(f"{args['c']} is not a directory")
    if not os.path.isdir(args["r"]):
        sys.exit(f"{args['r']} is not a directory")

    client_traces = os.listdir(args["c"])
    relay_traces = os.listdir(args["r"])
    client_traces.sort()
    relay_traces.sort()
    assert(len(client_traces) == len(relay_traces))

    # we define our simple machines: nothing, your (generated) c-code goes here
    client_machine = ""
    relay_machine = ""

    add_machines(client_machine, relay_machine)
    client_sim, relay_sim = simulate_traces(client_traces, relay_traces)
    print(f"got {len(client_sim)} client and {len(relay_sim)} relay traces")
    
    # for WF, can use dirtime or timecells instead of cells, also note the strip
    # parameter, TODO: update calls when we have a better way to deal with time
    client_wf = common.circpad_to_wf(client_sim[0], cells=True)
    relay_wf = common.circpad_to_wf(relay_sim[0], cells=True)
    print(f"{len(client_wf)} client and {len(relay_wf)} relay WF cells")

    # next, use a ML attack here to train and test on client_wf or relay_wf

    # Based on what you learned from the attack evaluation, update your machines
    # and repeat. We don't do that here though, so just cleanup by restoring the
    # source file for tor and re-building tor.
    with open(circpad_sim_loc, "w") as f:
        f.write(circpadsim_src)
    make_tor()

    print("all done, exiting")

def simulate_traces(client, relay):
    out_client, out_relay = [], []
    for i, c in enumerate(client):
        # run the simulation
        cmd = TOR_CIRCPADSIM_CMD_FORMAT.format(
            os.path.join(args["c"], c),
            os.path.join(args["r"], relay[i]),
        )
        result = subprocess.run(cmd, capture_output=True, text=True, shell=True)
        assert(result.returncode == 0)

        # parse out the simulated logs, get client and relay traces
        log = result.stdout.split("\n")
        client_sim = common.circpad_extract_log_traces(log, 
            source_client=True, source_relay=False)
        relay_sim = common.circpad_extract_log_traces(log, 
            source_client=False, source_relay=True, allow_ips=True)

        # for simulated traces, expect exactly one circuit
        assert(len(client_sim) == 1)
        assert(len(relay_sim) == 1)
        _, client_sim = client_sim.popitem()
        _, relay_sim = relay_sim.popitem()

        print(f"client len {len(client_sim)}, relay len {len(relay_sim)}")

        out_client.append(client_sim)
        out_relay.append(relay_sim)

    return out_client, out_relay

def add_machines(client, relay):
    # read source if we haven't already
    global circpadsim_src
    if circpadsim_src == "placeholder":
        with open(circpad_sim_loc, "r") as myfile:
            circpadsim_src = myfile.read()
        if circpadsim_src == "":
            sys.exit(f"no source found at {circpad_sim_loc}")

    # replace
    modified_src = circpadsim_src.replace(CLIENT_MACHINE_TOKEN, client)
    modified_src = modified_src.replace(RELAY_MACHINE_TOKEN, relay)

    # save
    with open(circpad_sim_loc, "w") as f:
        f.write(modified_src)

    # make
    make_tor()

def make_tor():
    cmd = f"cd {args['t']} && make"
    result = subprocess.run(cmd, stdout=subprocess.DEVNULL, shell=True)
    assert(result.returncode == 0)

if __name__ == "__main__":
    main()