# circpad-sim -- *work in progress*
A minimal simulator for padding machines in Tor's circuit padding framework.
This is very much a *work in progress*, not ready for use just yet. 

## Setup
- git clone https://github.com/mikeperry-tor/tor.git and checkout the `circpad-sim-squashed2` branch
- build tor as normal

## Usage
To run with a client and relay trace of your choice:

`./src/test/test --circpadsim src/test/circpad_sim_test_trace_client.inc src/test/circpad_sim_test_trace_relay.inc circuitpadding_sim/..`

Adding the `--debug` flag provides a lot of output:

`./src/test/test --debug --circpadsim src/test/circpad_sim_test_trace_client.inc src/test/circpad_sim_test_trace_relay.inc circuitpadding_sim/..`

The `--info` flag is enough to get the resulting simulated traces as part of the
output. To get the client traces, filter by `circpad_sim_results_trace_client`
and for the relay traces filter by `circpad_sim_results_trace_relay`. Below
shows an example of client traces:

`./src/test/test --info --circpadsim src/test/circpad_sim_test_trace_client.inc src/test/circpad_sim_test_trace_relay.inc circuitpadding_sim/.. | grep circpad_trace_event`

This gives output of the following format:

```
Nov 07 16:22:59.000 [info] circpad_trace_event(): timestamp=1668564966 source=client client_circ_id=2 event=circpad_cell_event_nonpadding_sent
Nov 07 16:23:00.000 [info] circpad_trace_event(): timestamp=1731585868 source=client client_circ_id=2 event=circpad_cell_event_nonpadding_received
Nov 07 16:23:00.000 [info] circpad_trace_event(): timestamp=1731854076 source=client client_circ_id=2 event=circpad_machine_event_circ_added_hop
Nov 07 16:23:00.000 [info] circpad_trace_event(): timestamp=1731901343 source=client client_circ_id=2 event=circpad_cell_event_nonpadding_sent
Nov 07 16:23:00.000 [info] circpad_trace_event(): timestamp=1952113125 source=client client_circ_id=2 event=circpad_cell_event_nonpadding_received
```

## Sketchpad during development
Ticket [#31788](https://trac.torproject.org/projects/tor/ticket/31788)

### ideas right now
- Ideally the simulator should be part of the unit test framework of Tor and
  callable from a standalone executable with arguments for specifying the input
  traces. When called from the standalone executable, disable all test-cases we
  have to disable and write resulting traces to disk or stdout. When called as a
  test, ship one or more traces with one or more machines and do heavy checks
  that we get exactly the trace we expect.
- Once input (during simulation, not testing) and output are sorted, clean up
  checks and add more tests. 
- Write a python script (`circpadtrace/circpadtrace2wf.py`) that uses client
  (and possible relay) traces to generate the cell traces for WF attacks,
  supporting different formats. Here we could have parameters for placing the
  attacker at different places between client and middle, add jitter etc as
  well.

### design considerations
The intended workflow is "modify machines -> sim many (order 1k-10k+) traces ->
evaluate -> repeat". Embarrassingly parallel: either spawn one instance of the
test per trace or write more c to to sim a folder of input. Probably better to
KISS in C, workflow loop will be dominated by evaluation (deep learning
training). Timers for sending padding cells are unreliable, 0-10 ms extra delay
([#31653](https://trac.torproject.org/projects/tor/ticket/31653)), so we ignore
detailed time for now, only want to get cell ordering right.

### How to format our input and output
A comprehensive sim framework should have input also from the relay (and guard
according to Mike, see
[#31788](https://trac.torproject.org/projects/tor/ticket/31788) comments). This
is because we also need to accurately simulate the events at the relay-side
padding machine, and correctly simulate the delay between relay-client. We
choose to be lazy here, since our time is short and timers are unreliable as-is
(see design considerations above). 

A circuit looks like this:

client - guard - middle - exit - destination

Our goal is to, from a trace of events with timestamps at the client, infer
events at the middle relay. We do this by estimating the RTT from the timestamps
of cells sent and then received, then adding some significant randomness to this
value to encourage machines to be conservative in their timers. About 1/4 of the
RRT is the time it takes for a (non-)padding cell to traverse to/from the client
to/from the relay.

### Trace collection
This is the lazy way with a simulated relay trace. To collect one trace from a
modified TB:
- copy `src/app/tor` and replace `tor` at `Browser/TorBrowser/Tor` of TB
- in torrc of TB (`TorBrowser/Data/Tor`), add ``Log [circ]info notice stdout''
- run TB with `/Browser/start-tor-browser --log hello.log`
- use `circuitpadtrace/torlog2circpadtrace.py` to transform to circpadtrace format
- use `circuitpadtrace/simrelaytrace.py` to simulate a relay trace from the
  client trace