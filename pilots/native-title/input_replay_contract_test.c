/* The maintained host replay controls without title, guest ABI, devices or HDD.
 * Include the same focused cases used by the existing input contract fixture. */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned failures;
#define CHECK(condition) do {if(!(condition)) {fprintf(stderr,"FAIL %s:%u: %s\n",__FILE__,__LINE__,#condition);++failures;}} while(0)
#define PILOT_INPUT_REPLAY_TEST_ONLY
#include "input_contract_test.inc"
int main(void)
{
#ifdef PILOT_INPUT_REFERENCE_CLOCK
    test_input_reference_clock();
#else
    test_input_replay_trigger();test_input_replay_two_triggers();
#endif
    return failures?1:0;
}
