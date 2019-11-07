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

## Example usage
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
./tor/src/test/test circuitpadding_sim/.. --circpadsim data/circpadtrace-example/eff.org.log data/sim-relay-circpadtrace-example/eff.org.log --info > example/log/eff.log
./circpadtrace/torlog2circpadtrace.py -i example/log/ -o example/client/
./circpadtrace/simrelaytrace.py -i example/client/ -o example/relay/
```

If you compare `example/client/eff.log` and
`data/circpadtrace-example/eff.org.log` they should be identical. Note that the
simulated relay traces may differ a bit due to the simulated latency between
client and relay.

## Trace collection
This is the lazy way with a simulated relay trace. To collect one trace using
Tor Browser (TB):
- copy `src/app/tor` and replace `tor` at `Browser/TorBrowser/Tor` of TB
- in torrc of TB (`TorBrowser/Data/Tor`), add ``Log [circ]info notice stdout''
- run TB with `/Browser/start-tor-browser --log example.log` 

Use the tools from `circpadtrace/` as in the example above to run it with the
simulator.

## Details
Ticket [#31788](https://trac.torproject.org/projects/tor/ticket/31788)

TODOs:
- complete `circpad-sim-evaluator.py` as an example of how to use this thing
- consider writing some tests for the simulator

### Usage considerations
The intended workflow is "modify machines -> sim many (order 1k-10k+) traces ->
evaluate -> repeat". Embarrassingly parallel problem to sim many traces. For
now, the simulator does one test per trace. For parallelism, run the simulator
many times. Likely workflow will be dominated by evaluation, including deep
learning training.

### Limitations
Unfortunately, timers for sending padding cells are unreliable, 0-10 ms extra
delay [#31653](https://trac.torproject.org/projects/tor/ticket/31653). We
currently only document how to simulate traces from a relay, no collection.
This is a flawed approximation, we encourage researchers to carefully consider
the implications of this. Ideally, the community will provide carefully
collected traces in the future with accurate timestamps at both clients and
relays. The real variability of tor's internal timers will remain a problem
though in the simulation.
