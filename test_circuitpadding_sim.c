#define CRYPT_PATH_PRIVATE
#define RELAY_PRIVATE
#define TOR_TIMERS_PRIVATE
#define CIRCUITPADDING_PRIVATE
#define CIRCUITPADDING_MACHINES_PRIVATE

#include "test/test.h"
#include "test/log_test_helpers.h"
#include "core/or/circuitpadding.h"
#include "lib/math/fp.h"
#include "lib/math/prob_distr.h"

#include "lib/string/compat_string.h"
#include "lib/defs/time.h"
#include "core/mainloop/netstatus.h"

#include "core/or/or.h"
#include "core/or/channel.h"
#include "core/or/circuitbuild.h"
#include "core/or/circuitlist.h"
#include "core/or/crypt_path.h"
#include "core/or/crypt_path_st.h"
#include "core/or/or_circuit_st.h"
#include "core/or/origin_circuit_st.h"
#include "core/or/relay.h"
#include "core/or/cell_st.h"

#include "feature/nodelist/nodelist.h"
#include "feature/nodelist/routerstatus_st.h"
#include "feature/nodelist/networkstatus_st.h"
#include "feature/nodelist/node_st.h"

/*
* To find the code to replace for the sim, search for REPLACE. Note that the
* machines you generate / create MUST have the names as defined below. The
* simulator checks that machines with the names got negotiated.
*/
#define CIRCPAD_SIM_CLIENT_MACHINE "circpad_sim_client_machine"
#define CIRCPAD_SIM_RELAY_MACHINE "circpad_sim_relay_machine"

/* Start our monotime mocking at 1 second past whatever monotime_init()
 * thought the actual wall clock time was, for platforms with bad resolution
 * and weird timevalues during monotime_init() before mocking. */
#define MONOTIME_MOCK_START (monotime_absolute_nsec()+\
                               TOR_NSEC_PER_USEC*TOR_USEC_PER_SEC)


// our testing trace if none is provided
#define CIRCPAD_SIM_TEST_TRACE_CLIENT_FILE "src/test/circpad_sim_test_trace_client.inc"
#define CIRCPAD_SIM_TEST_TRACE_RELAY_FILE "src/test/circpad_sim_test_trace_relay.inc"

// mocked functions and helpers, mostly lifted from test_circuitpadding.c
static void
circuitmux_attach_circuit_mock(circuitmux_t *cmux, circuit_t *circ,
                               cell_direction_t direction);
static int
circuit_package_relay_cell_mock(cell_t *cell, circuit_t *circ,
                           cell_direction_t cell_direction,
                           crypt_path_t *layer_hint, streamid_t on_stream,
                           const char *filename, int lineno);
static or_circuit_t * new_fake_orcirc(channel_t *nchan, channel_t *pchan);
circid_t get_unique_circ_id_by_chan(channel_t *chan);
static void free_fake_origin_circuit(origin_circuit_t *circ);
static void simulate_single_hop_extend(circuit_t *client, circuit_t *mid_relay,
                           int padding);
static void timers_advance_and_run(int64_t nsec_update);
static const node_t * node_get_by_id_mock(const char *identity_digest);
static void nodes_init(void);
static void circpad_event_callback_mock(const char *event, 
                                        uint32_t circuit_identifier);

typedef enum {
  CELL_EVENT_UNKNOWN = 0,
  CELL_EVENT_PADDING_SENT = 1,
  CELL_EVENT_NONPADDING_SENT = 2,
  CELL_EVENT_PADDING_RECV = 3,
  CELL_EVENT_NONPADDING_RECV = 4,
  MACHINE_EVENT_ADDED_HOP = 5,
  MACHINE_EVENT_CIRC_BUILT = 6,
  MACHINE_EVENT_HAS_STREAMS = 7,
  MACHINE_EVENT_HAS_NO_STREAMS = 8,
  MACHINE_EVENT_PURPOSE_CHANGED = 9,
  MACHINE_EVENT_HAS_NO_RELAY_EARLY = 10,
  CIRCPAD_SIM_INTERNAL_EVENT_NEGOTIATE = 11,
  CIRCPAD_SIM_INTERNAL_EVENT_NEGOTIATED = 12
} circpad_sim_event_t;

typedef struct circpad_sim_event {
  circpad_sim_event_t type; // type, extracted from event
  int64_t timestamp;
  const char *event;
  void *internal; // depends on event, simulator internal
  int smartlist_idx; // position in smartlist
} circpad_sim_event;

// The possible types of events this simulator cares about, values found in
// circpadtrace files, as instrumented in src/or/core/circuitpadding.c.
#define CIRCPAD_SIM_CELL_EVENT_PADDING_SENT "circpad_cell_event_padding_sent"
#define CIRCPAD_SIM_CELL_EVENT_NONPADDING_SENT "circpad_cell_event_nonpadding_sent"
#define CIRCPAD_SIM_CELL_EVENT_PADDING_RECV "circpad_cell_event_padding_received"
#define CIRCPAD_SIM_CELL_EVENT_NONPADDING_RECV "circpad_cell_event_nonpadding_received"
#define CIRCPAD_SIM_MACHINE_EVENT_ADDED_HOP "circpad_machine_event_circ_added_hop"
#define CIRCPAD_SIM_MACHINE_EVENT_CIRC_BUILT "circpad_machine_event_circ_built"
#define CIRCPAD_SIM_MACHINE_EVENT_HAS_STREAMS "circpad_machine_event_circ_has_streams"
#define CIRCPAD_SIM_MACHINE_EVENT_HAS_NO_STREAMS "circpad_machine_event_circ_has_no_streams"
#define CIRCPAD_SIM_MACHINE_EVENT_PURPOSE_CHANGED "circpad_machine_event_circ_purpose_changed"
#define CIRCPAD_SIM_MACHINE_EVENT_HAS_NO_RELAY_EARLY "circpad_machine_event_circ_has_no_relay_early"

// related sim functions
int get_circpad_trace(const char* loc, smartlist_t* trace);
static int find_circpad_sim_event(char* line, circpad_sim_event *e);
static int compare_circpad_sim_event(const void *_a, const void *_b);
static int circpad_sim_next_event(const char *event, int *cur, smartlist_t *trace);
static circpad_sim_event* circpad_sim_pop_event(smartlist_t *trace);
static void circpad_sim_push_event(circpad_sim_event *event, smartlist_t *trace);
static circpad_sim_event* circpad_sim_peak_event(smartlist_t *trace, int);
static int circpad_sim_continue(circpad_sim_event**, circuit_t**);

void test_circuitpadding_sim_main(void *arg);
MOCK_DECL(STATIC void, helper_add_client_machine, (void));
MOCK_DECL(STATIC void, helper_add_relay_machine, (void));
static void helper_add_client_machine_mock(void);
static void helper_add_relay_machine_mock(void);
static void circpad_sim_estimate_latency(void);
static int64_t circpad_sim_sample_latency(void);
static void circpad_sim_main_loop(void);

/*
* Core simulation function
*/

// testing-related variables to make the mocking of the rest of tor work
static channel_t dummy_channel;
static circuit_t *client_side;
static circuit_t *relay_side;
static node_t padding_node;
static node_t non_padding_node;
static int64_t curr_mocked_time;
static int64_t actual_mocked_monotime_start;
static int n_client_cells = 0;
static int n_relay_cells = 0;
static int deliver_negotiated = 1;
static int64_t sim_latency_mean;
static int64_t last_event_timestamp = 0;

// sim-specific
// FIXME: make into args or replaceable, get the path from the test
// or something like that
const char *client_trace_loc = CIRCPAD_SIM_TEST_TRACE_CLIENT_FILE;
const char *relay_trace_loc = CIRCPAD_SIM_TEST_TRACE_RELAY_FILE;

// the core working queues of traces, input and output
static smartlist_t *client_trace = NULL;
static smartlist_t *relay_trace = NULL;
static smartlist_t *out_client_trace = NULL;
static smartlist_t *out_relay_trace = NULL;

void
test_circuitpadding_sim_main(void *arg)
{
  (void)arg;

  client_trace = smartlist_new();
  relay_trace = smartlist_new();
  out_client_trace = smartlist_new();
  out_relay_trace = smartlist_new();

  tt_assert(get_circpad_trace(client_trace_loc, client_trace));
  tt_assert(get_circpad_trace(relay_trace_loc, relay_trace));
  int client_trace_start_len = smartlist_len(client_trace);
  int relay_trace_start_len = smartlist_len(relay_trace);

  // mocked machines for testing, enabling the simulator to test itself
  MOCK(helper_add_client_machine, helper_add_client_machine_mock);
  MOCK(helper_add_relay_machine, helper_add_relay_machine_mock);
  
  // we hook all instrumented output of the framework to create our output
  MOCK(circpad_event_callback, circpad_event_callback_mock);

  // start with the circuitpadding testing glue 
  MOCK(circuitmux_attach_circuit, circuitmux_attach_circuit_mock);
  MOCK(circuit_package_relay_cell, circuit_package_relay_cell_mock);
  MOCK(node_get_by_id, node_get_by_id_mock);
  dummy_channel.cmux = circuitmux_alloc();
  client_side = TO_CIRCUIT(origin_circuit_new());
  relay_side = TO_CIRCUIT(new_fake_orcirc(&dummy_channel, &dummy_channel));
  tt_assert(client_side);
  tt_assert(relay_side);
  relay_side->purpose = CIRCUIT_PURPOSE_OR;
  client_side->purpose = CIRCUIT_PURPOSE_C_GENERAL;
  nodes_init();
  monotime_init();
  monotime_enable_test_mocking();
  actual_mocked_monotime_start = MONOTIME_MOCK_START;
  monotime_set_mock_time_nsec(actual_mocked_monotime_start);
  monotime_coarse_set_mock_time_nsec(actual_mocked_monotime_start);
  curr_mocked_time = actual_mocked_monotime_start;
  timers_initialize();
  circpad_machines_init();
  helper_add_client_machine();
  helper_add_relay_machine();
  set_network_participation(1);

  /*
  * All Gluing done we home! The simulation works in three steps:
  * 1. Estimate the latency between client and relay, so that we can estimate
  *    the time it takes to send/receive padding cells injected by the machines.
  * 2. Build the circuit, taking account the time information in the provided
  *    traces and the estimated latency, including for handshake.
  * 3. The application data loop, everything else once a circuit is built and
  *    has streams attached (both covered before).
  */

  printf("\n\n ## in_client_trace ##\n");
  for (int i = 0; i < smartlist_len(client_trace); i++) {
    circpad_sim_event *ev;
    ev = circpad_sim_peak_event(client_trace, i);
    if (i < 15 || ev->type == CELL_EVENT_PADDING_SENT || 
        ev->type == CELL_EVENT_PADDING_RECV)
      printf("%016ld %s\n", ev->timestamp, ev->event);
  }
  
  printf("\n\n ## in_relay_trace ##\n");
  for (int i = 0; i < smartlist_len(relay_trace); i++) {
    circpad_sim_event *ev;
    ev = circpad_sim_peak_event(relay_trace, i);
    if (i < 15 || ev->type == CELL_EVENT_PADDING_SENT || 
        ev->type == CELL_EVENT_PADDING_RECV)
      printf("%016ld %s\n", ev->timestamp, ev->event);
  }

  circpad_sim_estimate_latency();

  // sanity check on estimated latency: (0,1s)
  tt_int_op(sim_latency_mean, OP_GT, 0);
  tt_int_op(sim_latency_mean, OP_LT, 1*TOR_NSEC_PER_USEC*TOR_USEC_PER_SEC);

  circpad_sim_main_loop();

  // FIXME: sanity check on resulting trace
  
  printf("\nclient input trace has %d events, output %d events\n", 
    client_trace_start_len, smartlist_len(out_client_trace));
  printf("relay input trace has %d events, output %d events\n", 
    relay_trace_start_len, smartlist_len(out_relay_trace));

  printf("\n\n ## out_client_trace ##\n");
  int max = smartlist_len(out_client_trace);
  for (int i = 0; i < max; i++) {
    circpad_sim_event *ev;
    ev = circpad_sim_pop_event(out_client_trace);
    if (i < 15 || ev->type == CELL_EVENT_PADDING_SENT || 
        ev->type == CELL_EVENT_PADDING_RECV)
      printf("%016ld %s\n", ev->timestamp, ev->event);
  }
  
  printf("\n\n ## out_relay_trace ##\n");
  max = smartlist_len(out_relay_trace);
  for (int i = 0; i < max; i++) {
    circpad_sim_event *ev;
    ev = circpad_sim_pop_event(out_relay_trace);
    if (i < 15 || ev->type == CELL_EVENT_PADDING_SENT || 
        ev->type == CELL_EVENT_PADDING_RECV)
      printf("%016ld %s\n", ev->timestamp, ev->event);
  }

  printf("\n");
  done:
    free_fake_origin_circuit(TO_ORIGIN_CIRCUIT(client_side));
    circuitmux_detach_all_circuits(dummy_channel.cmux, NULL);
    circuitmux_free(dummy_channel.cmux);
    timers_shutdown();
    monotime_disable_test_mocking();
    UNMOCK(node_get_by_id);
    UNMOCK(circuit_package_relay_cell);
    UNMOCK(circuitmux_attach_circuit);
    UNMOCK(helper_add_relay_machine);
    UNMOCK(helper_add_client_machine);
    SMARTLIST_FOREACH(client_trace, 
                      circpad_sim_event *, ev, tor_free(ev));
    SMARTLIST_FOREACH(relay_trace, 
                      circpad_sim_event *, ev, tor_free(ev));
    SMARTLIST_FOREACH(out_client_trace, 
                      circpad_sim_event *, ev, tor_free(ev));
    SMARTLIST_FOREACH(out_relay_trace, 
                      circpad_sim_event *, ev, tor_free(ev));
}

static void
circpad_sim_estimate_latency(void)
{
  /* The goal is to update sim_latency_mean based on observed RTT.
  *
  * We expect a trace like this:
  * 0000000000000000 circpad_machine_event_circ_added_hop
  * 0000000000000044 circpad_cell_event_nonpadding_sent
  * 0000000000063518 circpad_cell_event_nonpadding_received
  * 0000000000063803 circpad_machine_event_circ_added_hop
  * 0000000000063861 circpad_cell_event_nonpadding_sent
  * 0000000000176671 circpad_cell_event_nonpadding_received
  * 0000000000176992 circpad_machine_event_circ_added_hop
  * 
  * Potentially, there are padding cells or other events between.
  */
  circpad_sim_event *event;

  // check for three circpad_machine_event_circ_added_hop events
  int num_added_hop_events = 0, next = -1, i = 0;
  while(i < smartlist_len(client_trace)) {
    event = circpad_sim_peak_event(client_trace, i);
    if (event->type == MACHINE_EVENT_ADDED_HOP) {
      num_added_hop_events++;
      if (next == -1)
        next = i;
    }
    i++;
  }
  tt_int_op(num_added_hop_events, OP_EQ, 3);

  // find next circpad_cell_event_nonpadding_sent
  tt_assert(circpad_sim_next_event(CIRCPAD_SIM_CELL_EVENT_NONPADDING_SENT, 
                                  &next, client_trace));
  event = circpad_sim_peak_event(client_trace, next);
  int64_t sent = event->timestamp;

  // find circpad_cell_event_nonpadding_received
  tt_assert(circpad_sim_next_event(CIRCPAD_SIM_CELL_EVENT_NONPADDING_RECV, 
                                  &next, client_trace));
  event = circpad_sim_peak_event(client_trace, next);
  int64_t recv = event->timestamp;

  // RTT between client and middle is recv-sent microseconds
  sim_latency_mean = (recv-sent) / 2;
  // FIXME: consider also that we get client-exit RRT? More noise there though.
  // FIXME: support sim for other than middle

  done:
  ;
}

static int
circpad_sim_next_event(const char *event, int *cur, smartlist_t *trace)
{
  circpad_sim_event *e;
  while(*cur <= smartlist_len(trace)) {
    e = circpad_sim_peak_event(client_trace, *cur);
    if (strcmp(e->event, event) == 0)
      return *cur;

    (*cur)++;
  }
  return 0;
}

static int64_t 
circpad_sim_sample_latency(void)
{
  // FIXME: this is likely not reasonable at all
  const struct logistic my_logistic = {
    .base = LOGISTIC(my_logistic),
    .mu = sim_latency_mean,
    .sigma = 100, 
  };

  return MAX(0, tor_llround(dist_sample(&my_logistic.base)));
}

static circpad_sim_event* 
circpad_sim_pop_event(smartlist_t *trace)
{ 
  return smartlist_pqueue_pop(trace,
                              compare_circpad_sim_event,
                              offsetof(circpad_sim_event, smartlist_idx));
}

static circpad_sim_event* 
circpad_sim_peak_event(smartlist_t *trace, int i)
{ 
  return smartlist_get(trace, i);
}

static void 
circpad_sim_push_event(circpad_sim_event *event, smartlist_t *trace)
{ 
  return smartlist_pqueue_add(trace,
                            compare_circpad_sim_event,
                            offsetof(circpad_sim_event, smartlist_idx),
                            event);
}

static int
circpad_sim_continue(circpad_sim_event **next_event, circuit_t **next_side) {
  // goal: update next_event and next_side, if we should continue
  if (smartlist_len(client_trace) > 0 && smartlist_len(relay_trace) > 0) {
    if (circpad_sim_peak_event(client_trace, 0)->timestamp <
          circpad_sim_peak_event(relay_trace, 0)->timestamp) {
      *next_event = circpad_sim_pop_event(client_trace);
      *next_side = client_side;
    } else {
      *next_event = circpad_sim_pop_event(relay_trace);
      *next_side = relay_side;
    }
    return 1;
  } else if (smartlist_len(client_trace) > 0) {
    *next_event = circpad_sim_pop_event(client_trace);
    *next_side = client_side;
    return 1;
  } else if (smartlist_len(relay_trace) > 0) {
    *next_event = circpad_sim_pop_event(relay_trace);
    *next_side = relay_side;
    return 1;
  } else {
    // FIXME: check machines for timers (if any), and global max
  }

  return 0;
}

static void
circpad_sim_main_loop(void)
{
  /* The goal is to first build a circuit for the mocked circuitpadding
  * framework while using as much as possible of the input traces and injecting
  * as few simulated entries in the output as possible. Typically, the
  * instrumented traces from a Tor client will (i) miss cells for extending the
  * circuit to the guard and (ii) have significant delay between the circuit
  * being built (EVENT_CIRC_BUILT) and used (EVENT_HAS_STREAMS). We don't want
  * to change this due to how the simulator works.
  *
  * Once a circuit is up, it's just a matter of popping our client and relay
  * queues in the right order one event at a time. 
  */

  circpad_sim_event *next_event;
  circuit_t *next_side;
  cell_t *cell;

  // the loop is simple:
  // - find the next event and side (client or relay)
  // - move time forward until the next event
  // - act on the event
  while (circpad_sim_continue(&next_event, &next_side)) {
    timers_advance_and_run((next_event->timestamp - last_event_timestamp) * TOR_NSEC_PER_USEC);
    last_event_timestamp = next_event->timestamp;

    switch (next_event->type) {
      case MACHINE_EVENT_ADDED_HOP:
        simulate_single_hop_extend(client_side, relay_side, 1);
        circpad_machine_event_circ_added_hop(TO_ORIGIN_CIRCUIT(next_side));
        break;
      case MACHINE_EVENT_HAS_STREAMS:
        circpad_machine_event_circ_has_streams(TO_ORIGIN_CIRCUIT(next_side));
        break;
      case MACHINE_EVENT_HAS_NO_STREAMS:
        circpad_machine_event_circ_has_no_streams(
          TO_ORIGIN_CIRCUIT(next_side));
        break;
      case MACHINE_EVENT_PURPOSE_CHANGED:
        circpad_machine_event_circ_purpose_changed(
          TO_ORIGIN_CIRCUIT(next_side));
        break;
      case MACHINE_EVENT_HAS_NO_RELAY_EARLY:
        circpad_machine_event_circ_has_no_relay_early(
          TO_ORIGIN_CIRCUIT(next_side));
        break;
      case CELL_EVENT_PADDING_SENT:
        circpad_cell_event_padding_sent(next_side);
        break;
      case CELL_EVENT_NONPADDING_SENT:
        circpad_cell_event_nonpadding_sent(next_side);
        break;
      case CELL_EVENT_PADDING_RECV:
        cell = next_event->internal;
        if (next_side == client_side)
          circpad_deliver_recognized_relay_cell_events(client_side,
          cell->payload[0], TO_ORIGIN_CIRCUIT(client_side)->cpath->next);
        else
          circpad_deliver_recognized_relay_cell_events(relay_side,
          cell->payload[0], NULL);
        break;
      case CELL_EVENT_NONPADDING_RECV:
        circpad_cell_event_nonpadding_received(next_side);
        break;
      case CIRCPAD_SIM_INTERNAL_EVENT_NEGOTIATE:
        circpad_handle_padding_negotiate(relay_side, next_event->internal);
        break;
      case CIRCPAD_SIM_INTERNAL_EVENT_NEGOTIATED:
        circpad_handle_padding_negotiated(client_side, next_event->internal,
                                TO_ORIGIN_CIRCUIT(client_side)->cpath->next);
        // verify that our sim machines got negotiated
        tt_assert(client_side->padding_machine[0]);
        tt_assert(relay_side->padding_machine[0]);
        tt_str_op(client_side->padding_machine[0]->name, OP_EQ,
                  CIRCPAD_SIM_CLIENT_MACHINE);
        tt_str_op(relay_side->padding_machine[0]->name, OP_EQ,
                  CIRCPAD_SIM_RELAY_MACHINE);
        break;
      case MACHINE_EVENT_CIRC_BUILT:
        circpad_machine_event_circ_built(TO_ORIGIN_CIRCUIT(next_side));
        break;
      case CELL_EVENT_UNKNOWN:
      default:
        tor_assertf(0, "unknown sim event type, this should never happen");
    }

    tor_free(next_event->internal);
    tor_free(next_event);
  }

  done:
  ;
}

/*
* Defining the machines
*/

// The client machine to test. Can be generated, e.g., from a python script.
MOCK_IMPL(STATIC void, helper_add_client_machine, (void))
{
  //REPLACE-client-padding-machine-REPLACE
}

// The relay machine to test. Can be generated, e.g., from a python script.
MOCK_IMPL(STATIC void, helper_add_relay_machine, (void))
{
  //REPLACE-relay-padding-machine-REPLACE
}

/*
* Helpers for the simulation.
*/

static void 
circpad_event_callback_mock(const char *event, 
                            uint32_t circuit_identifier)
{
  circpad_sim_event *e = tor_malloc_zero(sizeof(circpad_sim_event));
  tor_assert(find_circpad_sim_event((char*)event, e));

  // relative timestamp in output, stored in usec
  e->timestamp = (curr_mocked_time - actual_mocked_monotime_start)/TOR_NSEC_PER_USEC;
  tor_assert(e->timestamp >= 0);

  // relay-side has identifier 0, clients > 0
  if (circuit_identifier) { 
    circpad_sim_push_event(e, out_client_trace);
  } else {
    circpad_sim_push_event(e, out_relay_trace);
  }

  //printf("%016ld %d %s\n", curr_mocked_time, circuit_identifier, event); 
}

int 
get_circpad_trace(const char* loc, smartlist_t* trace)
{
  char *line, *circpad_sim_trace_buffer, *circpad_sim_trace_read_rest;
  circpad_sim_trace_buffer = read_file_to_str(loc, 0, NULL);
  circpad_sim_trace_read_rest = circpad_sim_trace_buffer;

  while (1) {
    line = tor_strtok_r_impl(circpad_sim_trace_read_rest, "\n", 
                              &circpad_sim_trace_read_rest);
    if (!line) 
      break; // EOF

    circpad_sim_event e;
    if (find_circpad_sim_event(line, &e)) {
      circpad_sim_event *event = tor_malloc_zero(sizeof(circpad_sim_event));
      event->type = e.type;
      event->event = e.event;
      event->timestamp = strtol(line, NULL, 10);
      circpad_sim_push_event(event, trace);
    }
  }

  tor_free(circpad_sim_trace_buffer);
  return smartlist_len(trace) > 0;  // success if we found something to read
}

static int
find_circpad_sim_event(char* line, circpad_sim_event *e)
{
  // verbose but quick, in order of likely events
  if(strstr(line, CIRCPAD_SIM_CELL_EVENT_NONPADDING_RECV)) {
    e->type = CELL_EVENT_NONPADDING_RECV;
    e->event = CIRCPAD_SIM_CELL_EVENT_NONPADDING_RECV;
    return 1;
  }
  if(strstr(line, CIRCPAD_SIM_CELL_EVENT_NONPADDING_SENT)) {
    e->type = CELL_EVENT_NONPADDING_SENT;
    e->event = CIRCPAD_SIM_CELL_EVENT_NONPADDING_SENT;
    return 1;
  }
  if(strstr(line, CIRCPAD_SIM_CELL_EVENT_PADDING_RECV)) {
    e->type = CELL_EVENT_PADDING_RECV;
    e->event = CIRCPAD_SIM_CELL_EVENT_PADDING_RECV;
    return 1;
  }
  if(strstr(line, CIRCPAD_SIM_CELL_EVENT_PADDING_SENT)) {
    e->type = CELL_EVENT_PADDING_SENT;
    e->event = CIRCPAD_SIM_CELL_EVENT_PADDING_SENT;
    return 1;
  }
  if(strstr(line, CIRCPAD_SIM_MACHINE_EVENT_ADDED_HOP)) {
    e->type = MACHINE_EVENT_ADDED_HOP;
    e->event = CIRCPAD_SIM_MACHINE_EVENT_ADDED_HOP;
    return 1;
  }
  if(strstr(line, CIRCPAD_SIM_MACHINE_EVENT_CIRC_BUILT)) {
    e->type = MACHINE_EVENT_CIRC_BUILT;
    e->event = CIRCPAD_SIM_MACHINE_EVENT_CIRC_BUILT;
    return 1;
  }
  if(strstr(line, CIRCPAD_SIM_MACHINE_EVENT_HAS_STREAMS)) {
    e->type = MACHINE_EVENT_HAS_STREAMS;
    e->event = CIRCPAD_SIM_MACHINE_EVENT_HAS_STREAMS;
    return 1;
  }
  if(strstr(line, CIRCPAD_SIM_MACHINE_EVENT_HAS_NO_STREAMS)) {
    e->type = MACHINE_EVENT_HAS_NO_STREAMS;
    e->event = CIRCPAD_SIM_MACHINE_EVENT_HAS_NO_STREAMS;
    return 1;
  }
  if(strstr(line, CIRCPAD_SIM_MACHINE_EVENT_PURPOSE_CHANGED)) {
    e->type = MACHINE_EVENT_PURPOSE_CHANGED;
    e->event = CIRCPAD_SIM_MACHINE_EVENT_PURPOSE_CHANGED;
    return 1;
  }
  if(strstr(line, CIRCPAD_SIM_MACHINE_EVENT_HAS_NO_RELAY_EARLY)) {
    e->type = MACHINE_EVENT_HAS_NO_RELAY_EARLY;
    e->event = CIRCPAD_SIM_MACHINE_EVENT_HAS_NO_RELAY_EARLY;
    return 1;
  }

  return 0;
}

static int
compare_circpad_sim_event(const void *_a, const void *_b)
{
  const circpad_sim_event *a = _a, *b = _b;
  if (a->timestamp < b->timestamp)
    return -1;
  else if (a->timestamp == b->timestamp)
    return 0;
  else
    return 1;
}

static int
circuit_package_relay_cell_mock(cell_t *cell, circuit_t *circ,
                           cell_direction_t cell_direction,
                           crypt_path_t *layer_hint, streamid_t on_stream,
                           const char *filename, int lineno)
{
  (void)cell; (void)on_stream; (void)filename; (void)lineno;
  // allocate an event and figure out which one to schedule
  circpad_sim_event *e = tor_malloc_zero(sizeof(circpad_sim_event));

  // we need the cell later in our loop
  e->internal = tor_malloc_zero(sizeof(cell_t));
  memcpy(e->internal, cell, sizeof(cell_t));

  if (circ == client_side) {
    if (cell->payload[0] == RELAY_COMMAND_PADDING_NEGOTIATE) {
      // the client wants to negotiate a padding machine
      e->type = CIRCPAD_SIM_INTERNAL_EVENT_NEGOTIATE;
    } else {
      // the client wants to send a padding cell to the relay
      e->type = CELL_EVENT_PADDING_RECV;
      e->event = CIRCPAD_SIM_CELL_EVENT_PADDING_RECV;

      tt_int_op(cell_direction, OP_EQ, CELL_DIRECTION_OUT);
      tt_int_op(circpad_padding_is_from_expected_hop(circ, 
                layer_hint), OP_EQ, 1);
    }
    // schedule event at relay at some point in the future
    e->timestamp = last_event_timestamp + circpad_sim_sample_latency();
    circpad_sim_push_event(e, relay_trace);
    printf("HMM to relay_trace: %ld %s\n", e->timestamp, e->event);
    n_client_cells++;
  } else if (circ == relay_side) {
    tt_int_op(cell_direction, OP_EQ, CELL_DIRECTION_IN);
    if (cell->payload[0] == RELAY_COMMAND_PADDING_NEGOTIATED) {
      // the relay wants to reply to a padding negotiation request
      // XXX: blah need right layer_hint..
      // FIXME: we don't care about this, right? 
      if (deliver_negotiated) {
        e->type = CIRCPAD_SIM_INTERNAL_EVENT_NEGOTIATED;
      }
    } else {
      // the relay wants to send a padding cell to the client
      e->type = CELL_EVENT_PADDING_RECV;
      e->event = CIRCPAD_SIM_CELL_EVENT_PADDING_RECV;
    }
    // schedule event at client at some point in the future
    e->timestamp = last_event_timestamp + circpad_sim_sample_latency();
    circpad_sim_push_event(e, client_trace);
    printf("HMM to client_trace: %ld %s\n", e->timestamp, e->event);

    n_relay_cells++;
  }

 done:
  return 0;
}

/*
* Lifted from test_circuitpadding.c to get the circpad-part to work.
*/
static void
circuitmux_attach_circuit_mock(circuitmux_t *cmux, circuit_t *circ,
                               cell_direction_t direction)
{
  (void)cmux;
  (void)circ;
  (void)direction;

  return;
}

static or_circuit_t *
new_fake_orcirc(channel_t *nchan, channel_t *pchan)
{
  or_circuit_t *orcirc = NULL;
  circuit_t *circ = NULL;
  crypt_path_t tmp_cpath;
  char whatevs_key[CPATH_KEY_MATERIAL_LEN];

  orcirc = tor_malloc_zero(sizeof(*orcirc));
  circ = &(orcirc->base_);
  circ->magic = OR_CIRCUIT_MAGIC;

  //circ->n_chan = nchan;
  circ->n_circ_id = get_unique_circ_id_by_chan(nchan);
  cell_queue_init(&(circ->n_chan_cells));
  circ->n_hop = NULL;
  circ->streams_blocked_on_n_chan = 0;
  circ->streams_blocked_on_p_chan = 0;
  circ->n_delete_pending = 0;
  circ->p_delete_pending = 0;
  circ->received_destroy = 0;
  circ->state = CIRCUIT_STATE_OPEN;
  circ->purpose = CIRCUIT_PURPOSE_OR;
  circ->package_window = CIRCWINDOW_START_MAX;
  circ->deliver_window = CIRCWINDOW_START_MAX;
  circ->n_chan_create_cell = NULL;

  //orcirc->p_chan = pchan;
  orcirc->p_circ_id = get_unique_circ_id_by_chan(pchan);
  cell_queue_init(&(orcirc->p_chan_cells));

  circuit_set_p_circid_chan(orcirc, orcirc->p_circ_id, pchan);
  circuit_set_n_circid_chan(circ, circ->n_circ_id, nchan);

  memset(&tmp_cpath, 0, sizeof(tmp_cpath));
  if (cpath_init_circuit_crypto(&tmp_cpath, whatevs_key,
                                sizeof(whatevs_key), 0, 0)<0) {
    log_warn(LD_BUG,"Circuit initialization failed");
    return NULL;
  }
  orcirc->crypto = tmp_cpath.pvt_crypto;

  return orcirc;
}

static void
simulate_single_hop_extend(circuit_t *client, circuit_t *mid_relay,
                           int padding)
{
  (void)mid_relay;
  char whatevs_key[CPATH_KEY_MATERIAL_LEN];
  char digest[DIGEST_LEN];
  tor_addr_t addr;

  // Add a hop to cpath
  crypt_path_t *hop = tor_malloc_zero(sizeof(crypt_path_t));
  cpath_extend_linked_list(&TO_ORIGIN_CIRCUIT(client)->cpath, hop);

  hop->magic = CRYPT_PATH_MAGIC;
  hop->state = CPATH_STATE_OPEN;

  // add an extend info to indicate if this node supports padding or not.
  // (set the first byte of the digest for our mocked node_get_by_id)
  digest[0] = padding;

  hop->extend_info = extend_info_new(
          padding ? "padding" : "non-padding",
          digest, NULL, NULL, NULL,
          &addr, padding);

  cpath_init_circuit_crypto(hop, whatevs_key, sizeof(whatevs_key), 0, 0);

  hop->package_window = circuit_initial_package_window();
  hop->deliver_window = CIRCWINDOW_START;
}

static void
free_fake_origin_circuit(origin_circuit_t *circ)
{
  circpad_circuit_free_all_machineinfos(TO_CIRCUIT(circ));
  circuit_clear_cpath(circ);
  tor_free(circ);
}

static void
timers_advance_and_run(int64_t nsec_update)
{
  curr_mocked_time += nsec_update;
  monotime_coarse_set_mock_time_nsec(curr_mocked_time);
  monotime_set_mock_time_nsec(curr_mocked_time);
  timers_run_pending();
}

static const node_t *
node_get_by_id_mock(const char *identity_digest)
{
  if (identity_digest[0] == 1) {
    return &padding_node;
  } else if (identity_digest[0] == 0) {
    return &non_padding_node;
  }

  return NULL;
}

static void
nodes_init(void)
{
  padding_node.rs = tor_malloc_zero(sizeof(routerstatus_t));
  padding_node.rs->pv.supports_hs_setup_padding = 1;

  non_padding_node.rs = tor_malloc_zero(sizeof(routerstatus_t));
  non_padding_node.rs->pv.supports_hs_setup_padding = 0;
}

static void
helper_add_client_machine_mock(void)
{
  circpad_machine_spec_t *circ_origin_machine
      = tor_malloc_zero(sizeof(circpad_machine_spec_t));

  circ_origin_machine->name = CIRCPAD_SIM_CLIENT_MACHINE;
  circ_origin_machine->conditions.min_hops = 2;
  circ_origin_machine->conditions.state_mask =
      CIRCPAD_CIRC_BUILDING|CIRCPAD_CIRC_OPENED|CIRCPAD_CIRC_HAS_RELAY_EARLY;
  circ_origin_machine->conditions.purpose_mask = CIRCPAD_PURPOSE_ALL;
  circ_origin_machine->conditions.reduced_padding_ok = 1;

  circ_origin_machine->target_hopnum = 2;
  circ_origin_machine->is_origin_side = 1;

  // sends one padding cell after 100 ms, then just idles
   circpad_machine_states_init(circ_origin_machine, 2);
   circ_origin_machine->states[CIRCPAD_STATE_START].
      next_state[CIRCPAD_EVENT_NONPADDING_RECV] = CIRCPAD_STATE_BURST;
  circ_origin_machine->states[CIRCPAD_STATE_BURST].
      iat_dist.type = CIRCPAD_DIST_UNIFORM;
  circ_origin_machine->states[CIRCPAD_STATE_BURST].
      iat_dist.param1 = 100*1000;
  circ_origin_machine->states[CIRCPAD_STATE_BURST].
      iat_dist.param2 = 100*1000;
  circ_origin_machine->states[CIRCPAD_STATE_BURST].
      dist_max_sample_usec = 100*1000;

  circ_origin_machine->machine_num = smartlist_len(origin_padding_machines);
  circpad_register_padding_machine(circ_origin_machine,
                                   origin_padding_machines);
}

static void
helper_add_relay_machine_mock(void)
{
  circpad_machine_spec_t *circ_relay_machine
      = tor_malloc_zero(sizeof(circpad_machine_spec_t));

  circ_relay_machine->name = CIRCPAD_SIM_RELAY_MACHINE;
  circ_relay_machine->target_hopnum = 2;
  circ_relay_machine->is_origin_side = 0;

  // sends one padding cell 10 ms after receiving a padding cell
   circpad_machine_states_init(circ_relay_machine, 2);
   circ_relay_machine->states[CIRCPAD_STATE_START].
      next_state[CIRCPAD_EVENT_PADDING_RECV] = CIRCPAD_STATE_BURST;
  circ_relay_machine->states[CIRCPAD_STATE_BURST].
      iat_dist.type = CIRCPAD_DIST_UNIFORM;
  circ_relay_machine->states[CIRCPAD_STATE_BURST].
      iat_dist.param1 = 10*1000;
  circ_relay_machine->states[CIRCPAD_STATE_BURST].
      iat_dist.param2 = 10*1000;
  circ_relay_machine->states[CIRCPAD_STATE_BURST].
      dist_max_sample_usec = 10*1000;

  circ_relay_machine->machine_num = smartlist_len(relay_padding_machines);
  circpad_register_padding_machine(circ_relay_machine,
                                   relay_padding_machines);
}

#define TEST_CIRCUITPADDING_SIM(name, flags) \
  { #name, test_##name, (flags), NULL, NULL }


struct testcase_t circuitpadding_sim_tests[] = {
  TEST_CIRCUITPADDING_SIM(circuitpadding_sim_main, TT_FORK), 
  END_OF_TESTCASES
};
