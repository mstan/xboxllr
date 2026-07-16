// xlle-agent — a resident control/probe agent for the real Xbox (DESIGN §8, hardware oracle).
//
// Launched on the console (via XBMC's ExecBuiltIn(RunXBE(...)) over HTTP, hands-off), it
// hosts a TCP command server so the PC-side driver can read/write guest memory, push and
// execute arbitrary payloads, launch other XBEs, and run hardware probes — streaming results
// back for the cosim/hardware-conformance harness. Non-destructive: never touches storage;
// DASH returns the user to their dashboard.
//
// Protocol (line-based ASCII on tcp/4380, one client at a time):
//   PING                  -> "PONG <ver>"
//   INFO                  -> one JSON line: kernel version, tick count, agent port
//   R <hexaddr> <len>     -> "OK <hexbytes>"  (len <= 4096; direct guest-memory read)
//   W <hexaddr> <hexbytes>-> "OK <n>"         (direct guest-memory write)
//   EXEC <len>\n<raw>     -> "RET <hex>"       (copy raw bytes, call as int(*)(void))
//   L <path>              -> launches an XBE (does not return on success)
//   DASH                  -> returns to the dashboard (C:\evoxdash.xbe)
//   QUIT                  -> close this connection (agent keeps listening)
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <hal/debug.h>
#include <hal/video.h>
#include <hal/xbox.h>
#include <lwip/sockets.h>
#include <nxdk/net.h>
#include <xboxkrnl/xboxkrnl.h>

extern struct netif *g_pnetif;

#define AGENT_PORT 4380
#define AGENT_VER  "xlle-agent v0.2"
#define MAX_RW     4096

static void send_str(int fd, const char *s) { send(fd, s, (int)strlen(s), 0); }

static int hexnib(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
// Decode hex string -> bytes. Returns byte count, or -1 on bad input.
static int hex_to_bytes(const char *s, uint8_t *out, int maxout) {
    int n = 0;
    while (s[0] && s[1] && n < maxout) {
        int hi = hexnib(s[0]), lo = hexnib(s[1]);
        if (hi < 0 || lo < 0) return -1;
        out[n++] = (uint8_t)((hi << 4) | lo);
        s += 2;
    }
    return n;
}
static void bytes_to_hex(const uint8_t *in, int n, char *out) {
    static const char *H = "0123456789abcdef";
    for (int i = 0; i < n; i++) { out[2*i] = H[in[i] >> 4]; out[2*i+1] = H[in[i] & 0xF]; }
    out[2*n] = 0;
}

// Fault-guard (v0.2): is [a, a+len) safe to read? RAM is validated per-page via the kernel;
// the NV2A/MCPX MMIO block and the flash mirrors (minus the top 512B that hung v0.1) are
// whitelisted as memory-mapped. Everything else is refused rather than faulting the agent.
static int addr_readable(uint32_t a, uint32_t len) {
    uint32_t end = a + len;
    if (end < a) return 0;                                  // wrap
    if (a >= 0xFD000000 && end <= 0xFE000000) return 1;     // NV2A + MCPX MMIO
    if (a >= 0xFF000000 && end <= 0xFFFFFE00) return 1;     // flash mirrors (exclude top 512B)
    if (end <= 0x08000000) {                                // RAM window
        for (uint32_t p = a & ~0xFFFu; p < end; p += 0x1000)
            if (!MmIsAddressValid((PVOID)(uintptr_t)p)) return 0;
        return 1;
    }
    return 0;
}
// Writes: RAM (validated) and NV2A registers only. NEVER flash — a stray write could brick it.
static int addr_writable(uint32_t a, uint32_t len) {
    uint32_t end = a + len;
    if (end < a) return 0;
    if (a >= 0xFD000000 && end <= 0xFE000000) return 1;     // NV2A registers (register probing)
    if (end <= 0x08000000) {
        for (uint32_t p = a & ~0xFFFu; p < end; p += 0x1000)
            if (!MmIsAddressValid((PVOID)(uintptr_t)p)) return 0;
        return 1;
    }
    return 0;
}

// Read one \n-terminated line (returns length without newline, or <=0 on close/error).
static int recv_line(int fd, char *buf, int max) {
    int n = 0;
    while (n < max - 1) {
        char c;
        int r = recv(fd, &c, 1, 0);
        if (r <= 0) return (n > 0) ? n : r;
        if (c == '\n') break;
        if (c != '\r') buf[n++] = c;
    }
    buf[n] = 0;
    return n;
}

// Read exactly len raw bytes.
static int recv_all(int fd, uint8_t *buf, int len) {
    int got = 0;
    while (got < len) {
        int r = recv(fd, (char *)buf + got, len - got, 0);
        if (r <= 0) return r;
        got += r;
    }
    return got;
}

static void handle_client(int fd) {
    char line[512];
    static uint8_t rw[MAX_RW];
    static char hex[MAX_RW * 2 + 32];
    for (;;) {
        int n = recv_line(fd, line, sizeof(line));
        if (n <= 0) return;

        if (!strcmp(line, "PING")) {
            send_str(fd, "PONG " AGENT_VER "\n");
        } else if (!strcmp(line, "INFO")) {
            snprintf(hex, sizeof(hex),
                     "{\"agent\":\"%s\",\"kernel\":\"%u.%u.%u.%u\",\"tick\":%u,\"port\":%d}\n",
                     AGENT_VER, XboxKrnlVersion.Major, XboxKrnlVersion.Minor,
                     XboxKrnlVersion.Build, XboxKrnlVersion.Qfe,
                     (unsigned)KeTickCount, AGENT_PORT);
            send_str(fd, hex);
        } else if (!strncmp(line, "RB ", 3)) {
            uint32_t addr = 0, len = 0;                 // binary bulk read (efficient dumps)
            if (sscanf(line + 3, "%x %u", &addr, &len) == 2 && len > 0 && len <= (1u << 20) &&
                addr_readable(addr, len)) {
                snprintf(hex, sizeof(hex), "OK %u\n", len);
                send_str(fd, hex);
                uint32_t sent = 0;                      // stream raw bytes straight from memory
                while (sent < len) {
                    int chunk = (len - sent > 8192) ? 8192 : (int)(len - sent);
                    int w = send(fd, (const char *)(uintptr_t)(addr + sent), chunk, 0);
                    if (w <= 0) break;
                    sent += (uint32_t)w;
                }
            } else send_str(fd, "ERR bad/unsafe addr\n");
        } else if (!strncmp(line, "R ", 2)) {
            uint32_t addr = 0; int len = 0;
            if (sscanf(line + 2, "%x %d", &addr, &len) == 2 && len > 0 && len <= MAX_RW &&
                addr_readable(addr, (uint32_t)len)) {
                memcpy(rw, (const void *)(uintptr_t)addr, len);   // guarded read
                bytes_to_hex(rw, len, hex);
                send_str(fd, "OK "); send_str(fd, hex); send_str(fd, "\n");
            } else {
                send_str(fd, "ERR bad/unsafe addr\n");
            }
        } else if (!strncmp(line, "W ", 2)) {
            uint32_t addr = 0; char hbytes[400];
            if (sscanf(line + 2, "%x %399s", &addr, hbytes) == 2) {
                int nb = hex_to_bytes(hbytes, rw, MAX_RW);
                if (nb > 0 && addr_writable(addr, (uint32_t)nb)) {
                    memcpy((void *)(uintptr_t)addr, rw, nb);   // guarded write (RAM/NV2A only)
                    snprintf(hex, sizeof(hex), "OK %d\n", nb);
                    send_str(fd, hex);
                } else send_str(fd, "ERR bad hex / unsafe addr\n");
            } else send_str(fd, "ERR bad args\n");
        } else if (!strncmp(line, "EXEC ", 5)) {
            int len = atoi(line + 5);
            if (len > 0 && len <= 1 << 20) {
                uint8_t *code = malloc(len);            // Xbox memory is RWX (no DEP)
                if (code && recv_all(fd, code, len) == len) {
                    int (*fn)(void) = (int (*)(void))code;
                    int ret = fn();
                    snprintf(hex, sizeof(hex), "RET %x\n", (unsigned)ret);
                    send_str(fd, hex);
                } else send_str(fd, "ERR payload\n");
                free(code);
            } else send_str(fd, "ERR bad len\n");
        } else if (!strncmp(line, "L ", 2)) {
            send_str(fd, "LAUNCHING\n");
            XLaunchXBE(line + 2);                       // no return on success
            send_str(fd, "ERR launch failed\n");
        } else if (!strcmp(line, "DASH")) {
            send_str(fd, "RETURNING\n");
            XLaunchXBE("C:\\evoxdash.xbe");
        } else if (!strcmp(line, "QUIT")) {
            return;
        } else {
            send_str(fd, "ERR unknown\n");
        }
    }
}

int main(void) {
    XVideoSetMode(640, 480, 32, REFRESH_DEFAULT);
    debugClearScreen();
    debugPrint("%s\n", AGENT_VER);
    debugPrint("bringing up network...\n");
    nxNetInit(NULL);
    debugPrint("IP: %s\n", ip4addr_ntoa(netif_ip4_addr(g_pnetif)));

    int ls = socket(AF_INET, SOCK_STREAM, 0);
    int yes = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(AGENT_PORT);
    if (bind(ls, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        debugPrint("bind failed\n");
        Sleep(5000);
        return 1;
    }
    listen(ls, 2);
    debugPrint("xlle-agent listening on tcp/%d\n", AGENT_PORT);
    debugPrint("(commands: PING INFO R RB W EXEC L DASH QUIT)\n");

    for (;;) {
        int fd = accept(ls, NULL, NULL);
        if (fd < 0) continue;
        debugPrint("client connected\n");
        handle_client(fd);
        closesocket(fd);
        debugPrint("client disconnected\n");
    }
    return 0;
}
