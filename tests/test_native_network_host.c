/* Real backend lifetime only; no socket creation or network traffic. */
#include <stdio.h>
#include <string.h>
#include "network_host.h"
int main(void)
{
    PilotNetworkOffline state={0};
    if(!pilot_network_offline_initialize(&state,pilot_network_host_operations()))return 1;
    PilotNetworkLinkPolicy link={PILOT_NETWORK_ATTACHMENT_UNATTACHED};
    PilotNetworkResult query=pilot_network_offline_link(&state,pilot_network_host_link,&link);
    if(query.guard || query.hresult || query.set_last_error)return 7;
    uint32_t flags=0xfeedface;PilotNetworkLinkPolicy unavailable={0};
    if(pilot_network_host_link(NULL,&flags) || pilot_network_host_link(&unavailable,&flags) ||
       flags!=0xfeedface || pilot_network_host_link(&link,NULL))return 8;
    for(unsigned i=0;i<2;++i) {
        PilotNetworkResult result=pilot_network_offline_startup(&state,0);
        if(result.guard || result.hresult || result.set_last_error)return 2;
        query=pilot_network_offline_link(&state,pilot_network_host_link,&link);
        if(query.guard || query.hresult || query.set_last_error)return 9;
    }
    unsigned char bytes[66],saved[66];memset(bytes,0xa5,sizeof(bytes));
    if(!pilot_network_host_entropy(NULL,64,bytes+1) || bytes[0]!=0xa5 || bytes[65]!=0xa5)return 5;
    memcpy(saved,bytes,sizeof(bytes));
    if(pilot_network_host_entropy(NULL,65,bytes+1) || pilot_network_host_entropy(NULL,0,bytes+1) ||
       memcmp(saved,bytes,sizeof(bytes)))return 6;
    for(unsigned i=0;i<2;++i) {
        PilotNetworkResult result=pilot_network_offline_cleanup(&state);
        if(result.guard || result.hresult)return 3;
    }
    if(state.references || state.owner)return 4;
    query=pilot_network_offline_link(&state,pilot_network_host_link,&link);
    if(query.guard || query.hresult || query.set_last_error)return 10;
    puts("PASS real host Winsock lifetime, explicit unattached virtual-link policy and bounded OS entropy; no sockets or traffic");return 0;
}
