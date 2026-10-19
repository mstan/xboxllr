/* Real backend lifetime only; no socket creation or network traffic. */
#include <stdio.h>
#include "network_host.h"
int main(void)
{
    PilotNetworkOffline state={0};
    if(!pilot_network_offline_initialize(&state,pilot_network_host_operations()))return 1;
    for(unsigned i=0;i<2;++i) {
        PilotNetworkResult result=pilot_network_offline_startup(&state,0);
        if(result.guard || result.hresult || result.set_last_error)return 2;
    }
    for(unsigned i=0;i<2;++i) {
        PilotNetworkResult result=pilot_network_offline_cleanup(&state);
        if(result.guard || result.hresult)return 3;
    }
    if(state.references || state.owner)return 4;
    puts("PASS real host Winsock startup/cleanup ownership; no sockets or traffic");return 0;
}
