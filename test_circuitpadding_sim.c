#define CRYPT_PATH_PRIVATE
#define RELAY_PRIVATE
#define TOR_TIMERS_PRIVATE
#define CIRCUITPADDING_PRIVATE
#define CIRCUITPADDING_MACHINES_PRIVATE

#include "test/test.h"
#include "test/log_test_helpers.h"
#include "core/or/circuitpadding.h"

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
#define MONOTIME_MOCK_START   (monotime_absolute_nsec()+\
                               TOR_NSEC_PER_USEC*TOR_USEC_PER_SEC)


// our testing trace if none is provided
#define CIRCPAD_SIM_TEST_TRACE_FILE "circpad_sim_test_trace.inc"
#define CIRCPAD_SIM_MAX_TRACE_SIZE 5*1024*1024

#ifdef HAVE_CFLAG_WOVERLENGTH_STRINGS
DISABLE_GCC_WARNING(overlength-strings)
/* We allow huge string constants in the unit tests, but not in the code
 * at large. */
#endif
#include CIRCPAD_SIM_TEST_TRACE_FILE
#ifdef HAVE_CFLAG_WOVERLENGTH_STRINGS
ENABLE_GCC_WARNING(overlength-strings)
#endif

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
static void timers_advance_and_run(int64_t msec_update);
static const node_t * node_get_by_id_mock(const char *identity_digest);
static void nodes_init(void);
static void circpad_event_callback_mock(const char *event, 
                                        uint32_t circuit_identifier);

// simulation-related functions and structs
void test_circuitpadding_sim_main(void *arg);
MOCK_DECL(STATIC void, helper_add_client_machine, (void));
MOCK_DECL(STATIC void, helper_add_relay_machine, (void));
static void helper_add_client_machine_mock(void);
static void helper_add_relay_machine_mock(void);
void* create_test_env(const struct testcase_t *testcase);
int cleanup_test_env(const struct testcase_t *testcase, void *env);

typedef struct circpad_sim_env {
  const char *trace;
} circpad_sim_env;

typedef struct circpad_sim_event {
  int64_t timestamp;
  const char *event;
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
static int trace_get_next(circpad_sim_env* env, circpad_sim_event *e);
static int find_circpad_sim_event(char* line, circpad_sim_event *e);

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

// sim-specific
char* circpad_sim_trace_buffer = 0;
char* circpad_sim_trace_read_rest;

void
test_circuitpadding_sim_main(void *arg)
{
  circpad_sim_env *env = arg;
  tt_assert(env);

  (void)CIRCPAD_SIM_TEST_TRACE; // FIXME

  circpad_sim_event e;

  int i = 0;
  while(trace_get_next(env, &e))
    i++;
  printf("got %d events", i);

  /*
  * The simulator works as follows:
  * - setup by reading input file, estimate RTT
  * - re-use instrumentation in circpad_cell_event_* and circpad_machine_event_*
  *   callbacks to create our output by using mock
  * - create a client and relay with a mocked circuit, fully negotiated
  * - core simulation loop
  */

  // we provide mocked machines for testing, enabling the simulator to do some
  // basic tests of itself
  MOCK(helper_add_client_machine, helper_add_client_machine_mock);
  MOCK(helper_add_relay_machine, helper_add_relay_machine_mock);
  // FIXME: mock input and output as well, add a final check on the output
  // somehow
  MOCK(circpad_event_callback, circpad_event_callback_mock);

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

  // build a 3-hop circuit
  simulate_single_hop_extend(client_side, relay_side, 1);
  simulate_single_hop_extend(client_side, relay_side, 1);
  simulate_single_hop_extend(client_side, relay_side, 1);

  // verify that our sim machines got negotiated
  tt_str_op(client_side->padding_machine[0]->name, OP_EQ,
            CIRCPAD_SIM_CLIENT_MACHINE);
  tt_str_op(relay_side->padding_machine[0]->name, OP_EQ,
            CIRCPAD_SIM_RELAY_MACHINE);

  // read input file (make output file to figure out format, prob time event)
  // estimate RTT, use it and arg on variance (?) to sample later

  // re-use instrumentation in circpad_cell_event_* and circpad_machine_event_*
  // callbacks to create our output, use the MOCK functionality, leave empty for
  // now, create empty patch. We want to inject events we not recognize (like
  // address resolve in the loop to maintain those in output)

  // core loop going moving time forward
  // - one machine can have at most one timer scheduled
  // - ordering between client and relay a problem when factoring in time and
  //   events?

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
    tor_free(circpad_sim_trace_buffer);

}

/*
* Defining the tests and the machines
*/

void*
create_test_env(const struct testcase_t *testcase)
{
  struct circpad_sim_env *env = calloc(sizeof(*env), 1);
  if (!env)
      return NULL;
    env->trace = testcase->setup_data;
    return env;
}

int
cleanup_test_env(const struct testcase_t *testcase, void *env)
{
  (void)testcase;
  (void)env;

  return 1;
}

struct testcase_setup_t env_setup = {
  create_test_env,
  cleanup_test_env
};

#define TEST_CIRCUITPADDING_SIM(name, flags, trace) \
  { #name, test_##name, (flags), &env_setup, (void*)trace }

struct testcase_t circuitpadding_sim_tests[] = {
  //REPLACE-simulation-traces-goes-here-REPLACE
  TEST_CIRCUITPADDING_SIM(circuitpadding_sim_main, TT_FORK, 
                          CIRCPAD_SIM_TEST_TRACE_FILE),
  END_OF_TESTCASES
};

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
  struct timeval now;
  tor_gettimeofday(&now);
  int64_t us = (int64_t)(now.tv_sec) * (int64_t)1000000 + 
               (int64_t)(now.tv_usec);
  printf("%ld %d %s\n", us, circuit_identifier, event);
}

// reads the next event from env, returns 1 on success, 0 on EOF or error
static int
trace_get_next(circpad_sim_env* env, circpad_sim_event *e)
{
  char *line;

  // We have to read in the trace the first time. Read in the entire file at
  // once, trading memory for less back-and-forth calls. There's an assert if
  // the internal buffer (of CIRCPAD_SIM_MAX_TRACE_SIZE) is too small to fit a
  // trace.
  if (!circpad_sim_trace_buffer) {
    circpad_sim_trace_buffer = tor_malloc(CIRCPAD_SIM_MAX_TRACE_SIZE);
    if (strcmp(env->trace, CIRCPAD_SIM_TEST_TRACE_FILE) == 0) {
      tor_assert(strlcpy(circpad_sim_trace_buffer, 
              CIRCPAD_SIM_TEST_TRACE, 
              CIRCPAD_SIM_MAX_TRACE_SIZE) < CIRCPAD_SIM_MAX_TRACE_SIZE);
    } else {
      // TODO: read external trace file from env->trace into buffer
    }
    circpad_sim_trace_read_rest = circpad_sim_trace_buffer;
  }

  // search line-by-line until we find an event we care about
  do {
    line = tor_strtok_r_impl(circpad_sim_trace_read_rest, "\n", 
                              &circpad_sim_trace_read_rest);
    if (!line) return 0; // EOF
  } while(!find_circpad_sim_event(line, e));
  e->timestamp = strtol(line, NULL, 10);

  return 1; 
}

static int
find_circpad_sim_event(char* line, circpad_sim_event *e)
{
  // verbose but quick, in order of likely events
  if(strstr(line, CIRCPAD_SIM_CELL_EVENT_NONPADDING_RECV)) {
    e->event = CIRCPAD_SIM_CELL_EVENT_NONPADDING_RECV;
    return 1;
  }
  if(strstr(line, CIRCPAD_SIM_CELL_EVENT_NONPADDING_SENT)) {
    e->event = CIRCPAD_SIM_CELL_EVENT_NONPADDING_SENT;
    return 1;
  }
  if(strstr(line, CIRCPAD_SIM_CELL_EVENT_PADDING_RECV)) {
    e->event = CIRCPAD_SIM_CELL_EVENT_PADDING_RECV;
    return 1;
  }
  if(strstr(line, CIRCPAD_SIM_CELL_EVENT_PADDING_SENT)) {
    e->event = CIRCPAD_SIM_CELL_EVENT_PADDING_SENT;
    return 1;
  }
  if(strstr(line, CIRCPAD_SIM_MACHINE_EVENT_ADDED_HOP)) {
    e->event = CIRCPAD_SIM_MACHINE_EVENT_ADDED_HOP;
    return 1;
  }
  if(strstr(line, CIRCPAD_SIM_MACHINE_EVENT_CIRC_BUILT)) {
    e->event = CIRCPAD_SIM_MACHINE_EVENT_CIRC_BUILT;
    return 1;
  }
  if(strstr(line, CIRCPAD_SIM_MACHINE_EVENT_HAS_STREAMS)) {
    e->event = CIRCPAD_SIM_MACHINE_EVENT_HAS_STREAMS;
    return 1;
  }
  if(strstr(line, CIRCPAD_SIM_MACHINE_EVENT_HAS_NO_STREAMS)) {
    e->event = CIRCPAD_SIM_MACHINE_EVENT_HAS_NO_STREAMS;
    return 1;
  }
  if(strstr(line, CIRCPAD_SIM_MACHINE_EVENT_PURPOSE_CHANGED)) {
    e->event = CIRCPAD_SIM_MACHINE_EVENT_PURPOSE_CHANGED;
    return 1;
  }
  if(strstr(line, CIRCPAD_SIM_MACHINE_EVENT_HAS_NO_RELAY_EARLY)) {
    e->event = CIRCPAD_SIM_MACHINE_EVENT_HAS_NO_RELAY_EARLY;
    return 1;
  }

  // FIXME: add line to raw output

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

static int
circuit_package_relay_cell_mock(cell_t *cell, circuit_t *circ,
                           cell_direction_t cell_direction,
                           crypt_path_t *layer_hint, streamid_t on_stream,
                           const char *filename, int lineno)
{
  (void)cell; (void)on_stream; (void)filename; (void)lineno;

  if (circ == client_side) {
    if (cell->payload[0] == RELAY_COMMAND_PADDING_NEGOTIATE) {
      // Deliver to relay
      circpad_handle_padding_negotiate(relay_side, cell);
    } else {

      int is_target_hop = circpad_padding_is_from_expected_hop(circ,
                                                             layer_hint);
      tt_int_op(cell_direction, OP_EQ, CELL_DIRECTION_OUT);
      tt_int_op(is_target_hop, OP_EQ, 1);

      // Receive padding cell at middle
      circpad_deliver_recognized_relay_cell_events(relay_side,
              cell->payload[0], NULL);
    }
    n_client_cells++;
  } else if (circ == relay_side) {
    tt_int_op(cell_direction, OP_EQ, CELL_DIRECTION_IN);

    if (cell->payload[0] == RELAY_COMMAND_PADDING_NEGOTIATED) {
      // XXX: blah need right layer_hint..
      if (deliver_negotiated)
        circpad_handle_padding_negotiated(client_side, cell,
                                          TO_ORIGIN_CIRCUIT(client_side)
                                             ->cpath->next);
    } else if (cell->payload[0] == RELAY_COMMAND_PADDING_NEGOTIATE) {
      circpad_handle_padding_negotiate(client_side, cell);
    } else {
      // Receive padding cell at client
      circpad_deliver_recognized_relay_cell_events(client_side,
              cell->payload[0],
              TO_ORIGIN_CIRCUIT(client_side)->cpath->next);
    }

    n_relay_cells++;
  }

 done:
  timers_advance_and_run(1);
  return 0;
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
  char whatevs_key[CPATH_KEY_MATERIAL_LEN];
  char digest[DIGEST_LEN];
  tor_addr_t addr;

  // Pretend a non-padding cell was sent
  circpad_cell_event_nonpadding_sent(client);

  // Receive extend cell at middle
  circpad_cell_event_nonpadding_received(mid_relay);

  // Advance time a tiny bit so we can calculate an RTT
  curr_mocked_time += 10 * TOR_NSEC_PER_MSEC;
  monotime_coarse_set_mock_time_nsec(curr_mocked_time);
  monotime_set_mock_time_nsec(curr_mocked_time);

  // Receive extended cell at middle
  circpad_cell_event_nonpadding_sent(mid_relay);

  // Receive extended cell at first hop
  circpad_cell_event_nonpadding_received(client);

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

  // Signal that the hop was added
  circpad_machine_event_circ_added_hop(TO_ORIGIN_CIRCUIT(client));
}

static void
free_fake_origin_circuit(origin_circuit_t *circ)
{
  circpad_circuit_free_all_machineinfos(TO_CIRCUIT(circ));
  circuit_clear_cpath(circ);
  tor_free(circ);
}

static void
timers_advance_and_run(int64_t msec_update)
{
  curr_mocked_time += msec_update*TOR_NSEC_PER_MSEC;
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
      iat_dist.param1 = 0;
  circ_origin_machine->states[CIRCPAD_STATE_BURST].
      iat_dist.param2 = 100*1000;

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

  // sends one padding cell after 100 ms, then just idles
   circpad_machine_states_init(circ_relay_machine, 2);
   circ_relay_machine->states[CIRCPAD_STATE_START].
      next_state[CIRCPAD_EVENT_NONPADDING_RECV] = CIRCPAD_STATE_BURST;
  circ_relay_machine->states[CIRCPAD_STATE_START].
      iat_dist.type = CIRCPAD_DIST_UNIFORM;
  circ_relay_machine->states[CIRCPAD_STATE_START].
      iat_dist.param1 = 0;
  circ_relay_machine->states[CIRCPAD_STATE_BURST].
      iat_dist.param2 = 100*1000;

  circ_relay_machine->machine_num = smartlist_len(relay_padding_machines);
  circpad_register_padding_machine(circ_relay_machine,
                                   relay_padding_machines);
}
