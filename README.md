# circpad-sim
A minimal simulator for padding machines in Tor's circuit padding framework.

## Sketchpad during development
Advice from Mike: https://trac.torproject.org/projects/tor/ticket/31788 

TODO:
+ compile tor, run their unit-tests: `./src/test/test circuitpadding/..`
+ tor/doc/HACKING/WritingTests.md
- add two new test-files: one for the sim logic and one for the new machines, minimal changes to rest of tor
- write a simple test that adds and runs a machine at a client and middle relay

for the patch: git apply add-test_circuitpadding_sim.patch

