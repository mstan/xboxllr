/* Whole XAPI input service. Original driver bodies remain build-selectable.
 * Layout/side effects qualified against the owner's XDK4831 routines and the
 * existing OHCI Controller S report model. Host transport is Windows XInput.
 * No USB requests, authentication responses, or completion callbacks invented.
 */
#include "pilot_input_bindings.h"
#include "input_host.h"

struct PilotInputPort {
    uint8_t gamepad[18];
    uint32_t packet,generation;
    uint64_t next_poll;
    int connected;
};
struct PilotInputHandle {
    uint32_t id,port,generation,packet;
    uint8_t gamepad[18],polling[4];
};
static struct PilotInputPort input_ports[4];
static struct PilotInputHandle input_handles[4];
static SRWLOCK input_lock=SRWLOCK_INIT;
static uint32_t input_next_handle=0xc5000001u;
static unsigned input_capacity=4;
static int input_initialized;

static void input_buffer(uint32_t va,unsigned size)
{
    if (!va || (uint64_t)va+size>0x100000000ull) stop("invalid input buffer",va);
}
static unsigned input_class(uint32_t type)
{
    if (type==PILOT_INPUT_g_DeviceType_Gamepad) return 0;
    if (type==PILOT_INPUT_g_DeviceType_MU) return 1;
    if (type==PILOT_INPUT_g_DeviceType_IRDongle) return 2;
    stop("unqualified input device class",type);
}
static struct PilotInputHandle *input_handle(uint32_t id)
{
    for (unsigned i=0;i<4;++i) if (input_handles[i].id==id && id) return &input_handles[i];
    stop("invalid or closed input handle",id);
}
static int input_connected(const struct PilotInputHandle *h)
{
    return input_ports[h->port].connected && h->generation==input_ports[h->port].generation;
}
static void input_disconnect(unsigned port)
{
    struct PilotInputPort *p=&input_ports[port];
    if (p->connected) {
        uint32_t type=PILOT_INPUT_g_DeviceType_Gamepad,bit=1u<<port;
        p->connected=0;
        MEM32(type)&=~bit; MEM32(type+4)|=bit;
    }
}
static void input_sample(unsigned port,uint64_t now,int force)
{
    struct PilotInputPort *p=&input_ports[port];
    if (!force && now<p->next_poll) return;
    uint8_t state[18]; uint32_t status=pilot_input_host_state(port,state);
    if (status && status!=ERROR_DEVICE_NOT_CONNECTED) stop("host controller poll failed",status);
    unsigned interval=8;
    if (status) {
        input_disconnect(port); interval=250; /* bounded hotplug latency, no busy empty-slot polling */
    } else {
        int new_connection=!p->connected;
        if (new_connection) {
            uint32_t type=PILOT_INPUT_g_DeviceType_Gamepad,bit=1u<<port;
            if (++p->generation==0) stop("input connection generation exhausted",port);
            p->connected=1; p->packet=0;
            MEM32(type)|=bit; MEM32(type+4)|=bit;
        }
        /* Original XDK4831 filters analog buttons in its XID completion
         * callback and advances the packet for every completed report, even
         * an unchanged one. Host packet numbering is not the Xbox counter. */
        for(unsigned i=2;i<10;++i)if(state[i]<32)state[i]=0;
        ++p->packet;
        memcpy(p->gamepad,state,18);
        for (unsigned i=0;i<4;++i) {
            struct PilotInputHandle *h=&input_handles[i];
            if (!h->id || h->port!=port || !input_connected(h)) continue;
            memcpy(h->gamepad,state,18); h->packet=p->packet;
            interval=h->polling[1] ? h->polling[1] : 1;
        }
    }
    p->next_poll=now+(uint64_t)interval*10000;
}
static void input_refresh(void)
{
    uint64_t now=dispatch_monotonic();
    for (unsigned i=0;i<4;++i) input_sample(i,now,0);
}
static uint32_t input_open(const uint32_t args[4],uint32_t *error)
{
    uint32_t type=args[0],port=args[1],slot=args[2],poll=args[3];
    *error=0;
    if (type!=PILOT_INPUT_g_DeviceType_Gamepad && type!=PILOT_INPUT_g_DeviceType_IRDongle) {
        *error=ERROR_INVALID_PARAMETER; return 0;
    }
    if (type!=PILOT_INPUT_g_DeviceType_Gamepad || port>=4 || slot) {
        *error=ERROR_DEVICE_NOT_CONNECTED; return 0;
    }
    input_sample(port,dispatch_monotonic(),1);
    if (!input_ports[port].connected) { *error=ERROR_DEVICE_NOT_CONNECTED; return 0; }
    unsigned used=0; struct PilotInputHandle *free_slot=NULL;
    for (unsigned i=0;i<4;++i) {
        struct PilotInputHandle *h=&input_handles[i];
        if (!h->id) { if (!free_slot) free_slot=h; continue; }
        ++used;
        if (h->port==port && input_connected(h)) { *error=ERROR_SHARING_VIOLATION; return 0; }
    }
    if (!free_slot || used>=input_capacity) { *error=ERROR_OUTOFMEMORY; return 0; }
    uint8_t polling[4]={1,8,0,0};
    if (poll) { input_buffer(poll,4); memcpy(polling,memory+poll,4); }
    if (!(polling[0]&1) || (polling[0]&~3) || polling[3])
        stop("unqualified manual/reserved input polling mode",poll);
    uint8_t capabilities[18],rumble[4];
    uint32_t status=pilot_input_host_caps(port,capabilities,rumble);
    if (status==ERROR_NOT_SUPPORTED) stop("host peripheral is not a qualified gamepad",port);
    if (status) {
        if (status==ERROR_DEVICE_NOT_CONNECTED) input_disconnect(port);
        *error=status; return 0;
    }
    if (input_next_handle==0xc6000000u) stop("input handle identity exhausted",0);
    free_slot->id=input_next_handle++; free_slot->port=port;
    free_slot->generation=input_ports[port].generation;
    free_slot->packet=input_ports[port].packet;
    memcpy(free_slot->gamepad,input_ports[port].gamepad,18);
    memcpy(free_slot->polling,polling,4);
    fprintf(stderr,"[INPUT OPEN] port=%u handle=%08x interval=%u\n",port,free_slot->id,polling[1]);
    return free_slot->id;
}

void pilot_input_dispatch(unsigned entry,recomp_func_t set_last_error)
{
    if (entry>=PILOT_INPUT_COUNT) stop("unknown input boundary",entry);
    uint32_t args[4]={0},result=0,error=0;
    unsigned stack_bytes=pilot_input_calls[entry].stack_bytes;
    for (unsigned i=0;i<stack_bytes/4;++i) args[i]=MEM32(g_esp+4+i*4);
    AcquireSRWLockExclusive(&input_lock);
    if (entry!=PILOT_INPUT_XInitDevices && !input_initialized) stop("input used before initialization",entry);
    switch (entry) {
    case PILOT_INPUT_XInitDevices: {
        if (input_initialized) stop("repeated XInitDevices is unqualified",entry);
        if (args[0]>3) stop("unqualified input preallocation table",args[0]);
        if (args[0]) input_buffer(args[1],args[0]*8);
        unsigned seen=0;
        for (unsigned i=0;i<args[0];++i) {
            uint32_t type=MEM32(args[1]+i*8),count=MEM32(args[1]+i*8+4);
            unsigned cls=input_class(type);
            if (seen&(1u<<cls)) stop("duplicate input preallocation class",type);
            seen|=1u<<cls;
            if (count>(cls==1?8:cls==0?4:1)) stop("unqualified input preallocation count",count);
            if (!cls) input_capacity=count;
        }
        /* Explicit configuration: host gamepads, no attached MU or IR hardware.
         * The original 3-DWORD current/changed/previous masks stay guest-visible. */
        memset(memory+PILOT_INPUT_g_DeviceType_Gamepad,0,12);
        memset(memory+PILOT_INPUT_g_DeviceType_MU,0,12);
        memset(memory+PILOT_INPUT_g_DeviceType_IRDongle,0,12);
        input_initialized=1; input_refresh();
        fprintf(stderr,"[INPUT INIT] native-XInput gamepads=%x capacity=%u MU=0 IR=0\n",
                MEM32(PILOT_INPUT_g_DeviceType_Gamepad),input_capacity);
        break;
    }
    case PILOT_INPUT_XGetDevices:
        input_class(args[0]); input_refresh();
        result=MEM32(args[0]); MEM32(args[0]+4)=0; MEM32(args[0]+8)=result;
        fprintf(stderr,"[INPUT DEVICES] type=%08x mask=%x\n",args[0],result);
        break;
    case PILOT_INPUT_XGetDeviceChanges: {
        input_class(args[0]); input_buffer(args[1],4); input_buffer(args[2],4); input_refresh();
        uint32_t current=MEM32(args[0]),changed=MEM32(args[0]+4),previous=MEM32(args[0]+8);
        uint32_t both=changed&previous&current;
        uint32_t insertions=(current&~previous)|both,removals=(previous&~current)|both;
        MEM32(args[1])=insertions; MEM32(args[2])=removals;
        MEM32(args[0]+4)=0; MEM32(args[0]+8)=current;
        result=(insertions|removals)!=0; break;
    }
    case PILOT_INPUT_XInputOpen: result=input_open(args,&error); break;
    case PILOT_INPUT_XInputClose: {
        struct PilotInputHandle *h=input_handle(args[0]);
        if (input_connected(h)) {
            uint32_t status=pilot_input_host_rumble(h->port,0,0);
            if (status==ERROR_DEVICE_NOT_CONNECTED) input_disconnect(h->port);
            else if (status) stop("controller close could not stop rumble",status);
        }
        memset(h,0,sizeof(*h)); break;
    }
    case PILOT_INPUT_XInputGetState: {
        input_buffer(args[1],22);
        struct PilotInputHandle *h=input_handle(args[0]);
        input_sample(h->port,dispatch_monotonic(),0);
        MEM32(args[1])=h->packet; memcpy(memory+args[1]+4,h->gamepad,18);
        result=input_connected(h)?ERROR_SUCCESS:ERROR_DEVICE_NOT_CONNECTED; break;
    }
    case PILOT_INPUT_XInputGetCapabilities: {
        input_buffer(args[1],25);
        struct PilotInputHandle *h=input_handle(args[0]);
        input_sample(h->port,dispatch_monotonic(),0);
        if (!input_connected(h)) result=ERROR_DEVICE_NOT_CONNECTED;
        else {
            uint8_t caps[25]={2,0,0}; /* Controller S subtype, reserved WORD */
            memset(memory+args[1],0,25); MEM8(args[1])=2;
            result=pilot_input_host_caps(h->port,caps+3,caps+21);
            if (result==ERROR_NOT_SUPPORTED) stop("unqualified host controller capabilities",h->port);
            if (!result) memcpy(memory+args[1],caps,25);
            else if (result==ERROR_DEVICE_NOT_CONNECTED) input_disconnect(h->port);
        }
        MEM16(args[1]+1)=0; /* Original clears reserved even on disconnection. */
        break;
    }
    case PILOT_INPUT_XInputSetState: {
        input_buffer(args[1],70);
        struct PilotInputHandle *h=input_handle(args[0]);
        /* Event-backed async feedback needs real guest handle resolution first;
         * never confuse a guest event handle with a dispatcher-object address. */
        if (MEM32(args[1]+4)) stop("input feedback event handles are unqualified",MEM32(args[1]+4));
        input_sample(h->port,dispatch_monotonic(),0);
        MEM8(args[1]+64)=0; MEM8(args[1]+65)=6;
        result=input_connected(h) ? pilot_input_host_rumble(h->port,MEM16(args[1]+66),MEM16(args[1]+68))
                                  : ERROR_DEVICE_NOT_CONNECTED;
        if (result==ERROR_DEVICE_NOT_CONNECTED) input_disconnect(h->port);
        MEM32(args[1])=result; /* Completed only after the real host operation. */
        break;
    }
    default: stop("unqualified input operation",entry);
    }
    ReleaseSRWLockExclusive(&input_lock);
    if (error) {
        uint32_t original_stack=g_esp;
        PUSH32(g_esp,error); PUSH32(g_esp,0); set_last_error();
        if (g_esp!=original_stack) stop("input LastError callback ABI mismatch",g_esp);
    }
    g_eax=result; g_esp+=4+stack_bytes;
}
