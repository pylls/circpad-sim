#include "test/test.h"
#include "test/log_test_helpers.h"

void test_circuitpadding_sim_hello_world(void *arg);

void
test_circuitpadding_sim_hello_world(void *arg)
{
    (void)arg;
    tt_int_op(0, OP_EQ, 0);

    done:
    ;
}

#define TEST_CIRCUITPADDING_SIM(name, flags) \
    { #name, test_##name, (flags), NULL, NULL }

struct testcase_t circuitpadding_sim_tests[] = {
  TEST_CIRCUITPADDING_SIM(circuitpadding_sim_hello_world, TT_FORK),
  END_OF_TESTCASES
};
