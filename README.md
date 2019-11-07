# circpad-sim -- *work in progress*
A minimal simulator for padding machines in Tor's circuit padding framework.
This is very much a *work in progress*, not ready for use just yet. 

## Setup
The branch of the simulator is kept in another repo:

```
git clone https://github.com/mikeperry-tor/tor.git
cd tor
git checkout -t origin/circpad-sim-v2
```

Then build tor as normal. The simulator is tested as part of tor's unit testing
framework, you can check for it as follows:

```
$ ./src/test/test circuitpadding_sim/.. 
circuitpadding_sim/circuitpadding_sim_main: [forking] OK
1 tests ok.  (0 skipped)
```

## Example Usage
To run with a client and relay trace from `tor/`:

```
./src/test/test circuitpadding_sim/.. --circpadsim ../data/circpadtrace-example/eff.org.log ../data/sim-relay-circpadtrace-example/eff.org.log
```

Adding the `--debug` flag provides extra torlog output:

```
./src/test/test circuitpadding_sim/.. --circpadsim ../data/circpadtrace-example/eff.org.log ../data/sim-relay-circpadtrace-example/eff.org.log --debug
```

The `--info` flag is enough to get the resulting simulated traces as part of the
output. To get the resulting simulated client output, filter by `circpad_trace`:

```
./src/test/test circuitpadding_sim/.. --circpadsim ../data/circpadtrace-example/eff.org.log ../data/sim-relay-circpadtrace-example/eff.org.log --info | grep circpad_trace
```

This gives output of the following format:

```
Nov 07 16:22:59.000 [info] circpad_trace_event(): timestamp=1668564966 source=client client_circ_id=2 event=circpad_cell_event_nonpadding_sent
Nov 07 16:23:00.000 [info] circpad_trace_event(): timestamp=1731585868 source=client client_circ_id=2 event=circpad_cell_event_nonpadding_received
Nov 07 16:23:00.000 [info] circpad_trace_event(): timestamp=1731854076 source=client client_circ_id=2 event=circpad_machine_event_circ_added_hop
Nov 07 16:23:00.000 [info] circpad_trace_event(): timestamp=1731901343 source=client client_circ_id=2 event=circpad_cell_event_nonpadding_sent
Nov 07 16:23:00.000 [info] circpad_trace_event(): timestamp=1952113125 source=client client_circ_id=2 event=circpad_cell_event_nonpadding_received
```

There are helper python scripts in `circpadtrace/`. Below shows how to use the
scripts to recreate some of the example data in the repo.

Cleanup:
```
rm data/circpadtrace-example/* data/sim-relay-circpadtrace-example/*
```

Extract the traces from the log and then simulate the relay traces:
```
./circpadtrace/torlog2circpadtrace.py -i data/torlog-example/ -o data/circpadtrace-example/
./circpadtrace/simrelaytrace.py -i data/circpadtrace-example/ -o data/sim-relay-circpadtrace-example/
```

```
mkdir example example/log example/client example/relay
cd tor
./src/test/test circuitpadding_sim/.. --circpadsim ../data/circpadtrace-example/eff.org.log ../data/sim-relay-circpadtrace-example/eff.org.log --info > ../example/log/eff.log
cd ..
./circpadtrace/torlog2circpadtrace.py -i example/log/ -o example/client/
./circpadtrace/simrelaytrace.py -i example/client/ -o example/relay/
```

If you compare `example/client/eff.log` and
`data/circpadtrace-example/eff.org.log` they should be close to identical.

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