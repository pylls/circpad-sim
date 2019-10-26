#!/usr/bin/env python3
import argparse
import sys
import os
import shutil
import subprocess
import tempfile

ap = argparse.ArgumentParser()
ap.add_argument("-c", required=True, 
    help="input folder of client circpadtrace files")
ap.add_argument("-r", required=True, 
    help="input folder of relay circpadtrace files")
ap.add_argument("-t", required=True, 
    help="path to tor folder (bob/tor/, not bob/tor/src)")

args = vars(ap.parse_args())

TOR_CIRCPADSIM_SRC_LOC = "src/test/test_circuitpadding_sim.c"
CLIENT_MACHINE_TOKEN = "//REPLACE-client-padding-machine-REPLACE"
RELAY_MACHINE_TOKEN = "//REPLACE-relay-padding-machine-REPLACE"
TOR_CIRCPADSIM_CMD = os.path.join(args["t"], "src/test/test circuitpadding_sim/..")
TOR_CIRCPADSIM_CMD_FORMAT = f"{TOR_CIRCPADSIM_CMD} --info --circpadsim {{}} {{}}"

circpadsim_src = "placeholder"
circpad_sim_loc = os.path.join(args["t"], TOR_CIRCPADSIM_SRC_LOC)

tmpdir = tempfile.mkdtemp()

def main():
    '''A brief example with mostly comments of how one could evaluate padding 
    machines with the circuitpadding simulator.

    View this as a first outline in absense of more complete evaluators
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
    #client_traces = [os.path.join(args["c"], i) for i in os.listdir(args["c"])]
    client_traces = os.listdir(args["c"])
    #relay_traces = [os.path.join(args["r"], i) for i in os.listdir(args["r"])]
    relay_traces = os.listdir(args["r"])
    client_traces.sort()
    relay_traces.sort()
    assert(len(client_traces) == len(relay_traces))

    # we define our simple machines: nothing, your (generated) c-code goes here
    client_machine = ""
    relay_machine = ""

    add_machines(client_machine, relay_machine)
    client_sim_dir, relay_sim_dir = simulate_traces(client_traces, relay_traces)

    # parse to WF traces
    print(f"client_sim_dir has {len(os.listdir(client_sim_dir))}")
    print(f"relay_sim_dir has {len(os.listdir(relay_sim_dir))}")
    # FIXME: use common transform functions

    # cleanup tmp simmed dirs
    shutil.rmtree(client_sim_dir)
    shutil.rmtree(relay_sim_dir)

    # cleanup, restore source file
    with open(circpad_sim_loc, "w") as f:
        f.write(circpadsim_src)
    
    print("all done, exiting")

def simulate_traces(client, relay):
    client_sim_dir, relay_sim_dir = tempfile.mkdtemp(), tempfile.mkdtemp()
    for i, c in enumerate(client):
        cmd = TOR_CIRCPADSIM_CMD_FORMAT.format(
            os.path.join(args["c"], c),
            os.path.join(args["r"], relay[i]),
        )
        result = subprocess.run(cmd, capture_output=True, text=True, shell=True)
        assert(result.returncode == 0)
        client_sim, relay_sim = [], []
        for line in result.stdout.split("\n"):
            index = line.find("circpad_sim_results_trace_client()")
            if index > -1:
                client_sim.append(line[index:])
            index = line.find("circpad_sim_results_trace_relay()")
            if index > -1:
                relay_sim.append(line[index:])
        print(f"client len {len(client_sim)} and relay len {len(relay_sim)}")

        with open(os.path.join(client_sim_dir, c), 'w') as f:
            for l in client_sim:
                f.write(l)
        with open(os.path.join(relay_sim_dir, relay[i]), 'w') as f:
            for l in relay_sim:
                f.write(l)

    return client_sim_dir, relay_sim_dir

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
    cmd = f"cd {args['t']} && make"
    result = subprocess.run(cmd, stdout=subprocess.DEVNULL, shell=True)
    assert(result.returncode == 0)

if __name__ == "__main__":
    main()