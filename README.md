# Tor Circuit Padding Simulator

A minimal simulator for padding machines in Tor's [circuit padding
framework](https://github.com/torproject/tor/blob/master/doc/HACKING/CircuitPaddingDevelopment.md)

This simulator is extremely fast and efficient.

This is also research code. It may not do exactly what you expect.

Please read this document *CAREFULLY*.

## Overview

The circuit padding simulator consists of two repositories. This repository
holds python glue code that extracts traces from Tor logfiles and synthesizes
new traces.

There is a [separate Tor branch with
patches](https://github.com/mikeperry-tor/tor/commits/circpad-sim-v4) needed
for Tor. That Tor branch adds patches to instrument Tor such that it logs cell
events at the client, guard, middle, exit, or any other position.
Additionally, it provides a unit test that can take input trace files and
apply circuit padding machines to them, producing defended traces.

With both pieces together, your Tor client and Tor relays can record
undefended (non-padded) traces and then apply padding machines to these
traces, yielding defended traces, either in simulation, or on the live Tor
network.

## QuickStart and Test Examples

Assuming you have this repository checked out in a directory named
`circpad-sim`, and you're currently in that directory, then do:

```
cd ..                                 # from circpad-sim checkout, go up
git clone https://github.com/mikeperry-tor/tor.git
cd tor
git checkout -t origin/circpad-sim-v4 # Adjust origin and branches as needed
```

Then build tor as normal. The simulator is tested as part of tor's unit testing
framework, you can check for it as follows:

```
./src/test/test circuitpadding_sim/..
circuitpadding_sim/circuitpadding_sim_main: [forking] OK
1 tests ok.  (0 skipped)
```

This repository also has some example logs and traces that you can use with
built-in padding machines, using the unit test as a simulator.

First, we must convert our undefended Tor client logs into trace files. From
this circpad-sim checkout, do:
```
rm ./data/undefended/client-traces/*.trace   # Remove reference trace data
./torlog2circpadtrace.py -i ./data/undefended/client-logs/ -o ./data/undefended/client-traces/
git diff data                                # No diff for client traces
```

Now, we need to use these client traces to simulate some relay-side traces:
```
rm ./data/undefended/fakerelay-traces/*  # Remove reference trace data
./simrelaytrace.py -i ./data/undefended/client-traces/ -o data/undefended/fakerelay-traces
git diff data                            # Timestamps differ, but not event order
```

Once we have both client-side and relay-side trace files, we can simulate
applying a padding machine defense to them, using the previously compiled Tor
test binary:

```
../tor/src/test/test --info circuitpadding_sim/.. --circpadsim ./data/undefended/client-traces/eff.org.trace ./data/undefended/fakerelay-traces/eff.org.trace 1 > ./data/defended/combined-logs/eff.org.log
```

This gives Tor log output of the following format in `./data/defended/combined-logs/eff.org.log`:

```
Dec 10 10:13:50.240 [info] circpad_trace_event(): timestamp=11339844396 source=relay client_circ_id=1 event=circpad_cell_event_nonpadding_sent
Dec 10 10:13:50.240 [info] circpad_trace_event(): timestamp=11339850638 source=relay client_circ_id=1 event=circpad_cell_event_nonpadding_sent
Dec 10 10:13:50.240 [info] circpad_trace_event(): timestamp=11375969198 source=client client_circ_id=1 event=circpad_cell_event_nonpadding_received
Dec 10 10:13:50.241 [info] circpad_trace_event(): timestamp=11376008271 source=client client_circ_id=1 event=circpad_cell_event_nonpadding_received
```

Note that this log file contains *both* relay and client traces!

To convert that log output into a trace file that can be used as input to
classifiers or other code, do:

```
rm ./data/defended/client-traces/*               # Remove any old traces
rm ./data/defended/relay-traces/*                # Remove any old traces
grep "source=client" ./data/defended/combined-logs/eff.org.log > ./data/defended/client-logs/eff.org.log
grep "source=relay" ./data/defended/combined-logs/eff.org.log > ./data/defended/relay-logs/eff.org.log
./torlog2circpadtrace.py --ip -i ./data/defended/relay-logs/ -o ./data/defended/relay-traces/
./torlog2circpadtrace.py -i ./data/defended/client-logs/ -o ./data/defended/client-traces/
git diff ./data/defended/client-traces/          # No diff
git diff ./data/defended/relay-traces/           # No diff
```

You should now have defended trace files for the client side and the relay
side.

Finally, to convert the defended client trace files into standard WF
classifier 1,-1 format files without timestamps, run:

```
rm ./data/defended/client-wfcells/*
./circpadtrace2wf.py -i ./data/defended/client-traces/ -o ./data/defended/client-wfcells/ -t cells
git diff ./data/defended/client-wfcells/              # No diff
```

To verify operation, if you diff your client traces to the ones in this repo,
they should be identical. Note that the simulated relay traces may differ a
bit due to the simulated latency between client and relay.

## Adding Your Own Padding Machines

Any padding machine you add to the [simulator Tor
branch](https://github.com/mikeperry-tor/tor/commits/circpad-sim-v4)
will apply in the simulator to the test traces above, as well as to the live
network, in exactly the same way.

To get up and running with a real machine quickly, see the [Circuit Padding Quickstart
Guide](https://github.com/torproject/tor/blob/master/doc/HACKING/CircuitPaddingQuickStart.md).

For more examples, see [Section 5 of the developer
doc](https://github.com/torproject/tor/blob/master/doc/HACKING/CircuitPaddingDevelopment#5-example-padding-machines)
and the rest of that documentation.

## Collecting Client-Side Traces

To collect a client side trace using Tor Browser (TB):
- copy `src/app/tor` and replace `tor` at `Browser/TorBrowser/Tor` of TB
- in torrc of TB (`TorBrowser/Data/Tor`), add ``Log [circ]info notice stdout''
- run TB with `/Browser/start-tor-browser --log example.log`

Additional information on running a custom Tor with Tor Browser can be found
in the [Tor Browser Hacking Guide](https://trac.torproject.org/projects/tor/wiki/doc/TorBrowser/Hacking#RunningMultipleTorBrowsers).

Note that the example.log file created by Tor Browser will have multiple
different circuits recorded in it. Because the circuit padding simulator only
works on one circuit at a time, you must separate each circuit into its own
log and trace files.

A [set of Tor Browser docker-based orchestration
scripts](https://github.com/pylls/padding-machines-for-tor/tree/master/collect-traces)
to generate a set of undefended traces is also available, but be aware that
additional sanity checking and cleanup is needed to ensure that each site only
uses one circuit.

Specifically, by default the `torlog2circpadtrace.py` script takes only single
longest trace from a log file, and makes no effort to make sure that the
client_circuit_id's match any relay side traces. If you have multiple circuits
in your log, you should ensure they are matching the relay side properly.

If you want a specific circuit id other than the longest trace, you must
specifically specicfy the circuit id with `torlog2circpadtrace.py --cid`.

*NOTE*: The circuit id in the log output is the client circuit id. If you
restart your Tor client, you will get duplicate circuit ids, causing your
traces to get merged together.

## Collecting Relay-Side Traces

In order for padding machines to work, they need traces for both a relay and a
client (because there are padding machines both at the client, and at a
relay).

### Synthetic Relay Traces

If your experiments are not using timing information, you can create a
synthetic relay trace for input into the simulator using a real client trace:

```
./simrelaytrace.py -i ./data/undefended/client-traces/ -o data/undefended/fakerelay-traces
```

By default, this strips off the first two cells (the onionskin handshake), and
thus creates a middle node trace suitable for input to the padding simulator.

If you want a guard node trace (for eg a classifier), add the --gaurd
argument. This will cut the added latency in half, and not remove the first
onionskin handshake.

### Real Middle Relay Traces

If you are reproducing your padding machines on the live network, you will
want to run the [circpad simulator Tor
branch](https://github.com/mikeperry-tor/tor/commits/circpad-sim-v4) with your
padding machines applied as a middle relay.

*NOTE*: If your experiments are sensitive to time, first see the [limitations
section](#limitations) and the [circpad timing section for more
info](https://github.com/torproject/tor/blob/master/doc/HACKING/CircuitPaddingDevelopment.md#72-timing-and-queuing-optimizations)
before just blindly using the timestamps produced from live crawls.

Your Middle Node Torrc should look roughy like:

```
Nickname researchermiddle
ORPort 9001
ExitRelay 0
Log notice stdout
Log [circ]info file relay-circpad.log
```

Then, when Tor starts up and tells you your relay fingerprint, you should go
back to your Tor Browser torrc, and add:

```
MiddleNodes YOUR_FINGERPRINT_HERE
Log [circ]info file client-circpad.log
```

It will [take some time for new
relays](https://blog.torproject.org/lifecycle-new-relay) to obtain the Fast
flag from the authorites (which they must have to get used by your client).

With pinned middle nodes, the simulator branch will send a special logging
command cell only for your client branch circuits, to those middle nodes,
instructing them to log only your Tor circuits. The circuit IDs will also be
sent across in this cell, so numerically they will match on the client and the
relay.

The special logging negotiation cell event
(`event=circpad_negotiate_logging`) and its following cell event are present
in client-side log files, but are stripped from the trace files by
`torlog2circpadtrace.py`. They are absent from relay log and trace files.

*NOTE*: Just like the client side trace converion, that script takes only
the longest trace, and makes no effort to make sure that the
client_circuit_id's match. If you have multiple circuits in your relay log,
you should ensure they are matching properly.

*NOTE*: If you use multiple clients (or even just restart the same client),
their circuit ids will collide on your relay logs, causing you to mismatch
your traces.

### Real Guard Node Traces

You can alternatively (or additionally) log at the entry node by editing the
`log_at_hops` variable of the function
[circpad_negotiate_logging()](https://github.com/mikeperry-tor/tor/blob/circpad-sim-v4/src/core/or/circuitpadding.c#L2204)
in `tor/src/core/or/circuitpadding.c` in the Tor circpad simulator branch.
You can list as many hop positions as you have relays for there.

The simplest way to use a specific relay as a "guard" is to use the torrc
Bridge directive. You can use this directive for relays that are
in the Tor consensus. In this way, you can test and measure the effects
of other concurrent Tor activity is, without necessarily waiting for that
relay to have the Guard flag.

For example, if your relay is running at 1.2.3.4 port 9001, you would
specify the following in your client's torrc:

```
UseBridges 1
Bridge 1.2.3.4:9001
```

Clients only request logging from any node if the MiddleNodes directive is
set. This means to log from just the Guard node, you must either change the
`circpad_negotiate_logging()` check, or always pin generic middles, otherwise
the logging negotiate cell will not get sent.

*NOTE*: If you list any positions that you do not control in that `log_at_hops`
array, or don't properly restrict your client to use only your relays for
those hops, you will get error cells back, which may affect your results.

*NOTE*: If you set up logging to multiple hops at once, the earlier nodes
in the path will observe and record these additional logging cells as
`circpad_nonpadding_cell_*` events (one receive, and one sent). Removing these
is tricky in the general case, but you may be able to do it sifting through
the corresponding client logs. We do not do anything for this yet.

## Usage considerations

It's an embarrassingly parallel problem to sim many traces, so the simulator only
simulates one trace per run. For parallelism, run the simulator many times.
Likely workflow will be dominated by evaluation, including deep learning
traning.

### Running experiments

In `circpad-sim-exp.py` you'll find a brief example with mostly comments of how
one could script the evaluation of padding machines with the circuitpadding
simulator.

While in this `circpad-sim` directory, run it as follows:

```
./circpad-sim-exp.py -c ./data/undefended/client-traces/ -r ./data/undefended/fakerelay-traces/ -t ../tor
```

### Working With Trace Files

The trace files contain full Circuit Padding Framework event logs at
nanosecond precision. They need some processing before they can be used
in a classifier.

In particular, the classifier should only see `circpad_cell_event_*` events
and it obviously should be not be given visibility into if they are padding or
not. It should only see that they were sent or recieved.

To convert the trace files to standard "WF-format" classifier input files
without timestamps:

```
./circpadtrace2wf.py -i ./data/defended/client-traces/ -o ./data/defended/client-wfcells/ -t cells
```

To include either the time or directional time, change that -t argument
accordingly.

*NOTE*: Be aware that the nanosecond timestamps are way higher precision than a
network adversary may see in practice, and may allow the classifier to learn
traits based on fine-grained application timings. You may want to truncate or
eliminate these timestamps when they are used for classifier input.

See also the [timing accuracy issues](#timing-accuracy-issues) section for
more issues on working with timestamps.

### Scope of Trace files

The simulator branch records all padding and non-padding cells sent on a
circuit immediately after the first circuit handshake has completed
at the hop that is performing the logging, until the circuit is
closed/destroyed at that hop. The DESTROY cell itself is not counted.
Any forwarded `RELAY_COMMAND_TRUNCATED` cells are.

At the client, this means the first `circpad_cell_event_nonpadding_sent`
event is the onionskin that is sent to the middle hop, since logging
started *after* the onionskin completed with the guard/bridge.

At the guard/bridge, the first `circpad_cell_event_nonpadding_received`
event is the onionskin that is to be forwarded to the middle hop.

Notice that this means that the client and guard traces will exactlty mirror
eachother.

At the middle relay, the first `circpad_cell_event_nonpadding_received`
event is the onionskin that is to be forwarded to the exit/third hop. This
means that it is missing one send/recv pair from the client trace, but should
otherwise mirror it.

If your experiments rely on circuit setup timing information for the handshake
before logging begins, please contact us for ways to provide this.  Otherwise
you can probably get away with inserting your own synthetic cell events there.

## Limitations

The simulator has some limitations that you need to be aware of.

### Timing Accuracy Issues

The simulator inherits some [timing
issues](https://github.com/mikeperry-tor/tor/blob/circuitpadding-dev-doc/doc/HACKING/CircuitPaddingDevelopment.md#7-future-features-and-optimizations)
from the Circuit Padding Framework and adds some of its own.

Unfortunately, timers for sending padding cells are unreliable, with [0-10 ms
extra delay](https://bugs.torproject.org/32670).

Additionally, the padding framework currently has issues sending cells
back-to-back [with 0 delay](https://bugs.torproject.org/31653).

Finally, all cell event and log collection points for the event callbacks
also impose some inaccuracy due to [queuing
delay](https://bugs.torproject.org/29494).

In this simulator, we also do not model or factor in the varying latency
of running this attack on the network near or at the Guard node. You get
nanosecond precision timestamps at the client, which may encode CPU and
memory usage patterns that give away more information than an adversary
normally would have.

Until these timing issues are resolved, it is wise to omit timestamps
from classifier input, or at least truncate their resolution considerably.

### Multiplexing Multiple Circuits

The padding simulator only works on one circuit at a time. It also only
extracts the longest circuit id trace from a log file, if multiple circuits
are present.

It also resets time to 0 for the start of each circuit. This means there is
more work needed to model the multiplexing effects of Guard node TLS.

To study the effects of multiplexing, you will need to write some scripts to
sepeate logfiles by circuit ID, but additionally store the circuit start time
separately, and use that start time to merge individual defended traces back
into a single properly aligned input into your classifier. Your classifier
should not get circuit ID separation information in this case.

### Proper Client-Relay Circuit Matching

Both the client side log converion and the relay side log conversion
take only the longest trace, and makes no effort to make sure that the
`client_circ_id` matches the desired circuit on relay and client. If you
have multiple circuits in your relay log, you should ensure they are
matching properly.

### Other TODOs

TODOs:
- complete `circpad-sim-evaluator.py` as an example of how to use this thing
- consider writing some tests for the simulator


