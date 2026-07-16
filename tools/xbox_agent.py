#!/usr/bin/env python3
"""PC-side driver for xlle-agent running on the real Xbox (see agent/main.c).

Talks the agent's line protocol over TCP, and can deploy the agent XBE via FTP and
(best-effort) launch it via XBMC's HTTP command interface.

Examples:
  python xbox_agent.py --host 192.168.4.81 ping
  python xbox_agent.py info
  python xbox_agent.py read 0x80010000 64        # 64 bytes of guest memory (hex)
  python xbox_agent.py write 0x00011000 90909090 # poke bytes
  python xbox_agent.py exec payload.bin          # push+run an int(void) payload
  python xbox_agent.py deploy default.xbe        # FTP upload + try HTTP launch
"""
import argparse, ftplib, socket, sys, time, urllib.request

AGENT_PORT = 4380
FTP_USER, FTP_PASS = "xbox", "xbox"

def agent_cmd(host, line, payload=None, timeout=8):
    s = socket.create_connection((host, AGENT_PORT), timeout=timeout)
    s.sendall((line + "\n").encode())
    if payload is not None:
        s.sendall(payload)
    time.sleep(0.2)
    data = b""
    s.settimeout(timeout)
    try:
        while True:
            c = s.recv(65536)
            if not c:
                break
            data += c
            if data.endswith(b"\n"):
                break
    except socket.timeout:
        pass
    s.close()
    return data.decode("latin-1").strip()

def deploy(host, xbe_path, remote_dir="/E/Apps/xlle-agent"):
    f = ftplib.FTP(); f.connect(host, 21, timeout=20); f.login(FTP_USER, FTP_PASS)
    # ensure dir (best-effort; never delete)
    for part in remote_dir.strip("/").split("/"):
        try: f.mkd("/" + part) if False else None
        except Exception: pass
    try: f.mkd(remote_dir)
    except Exception: pass
    f.cwd(remote_dir)
    with open(xbe_path, "rb") as fh:
        f.storbinary("STOR default.xbe", fh)
    print(f"uploaded {xbe_path} -> {remote_dir}/default.xbe")
    f.quit()
    # best-effort HTTP launch via XBMC ExecBuiltIn(RunXBE(...))
    win_path = "E:\\Apps\\xlle-agent\\default.xbe"
    for form in ("xbmcHttp", "xbmcForm"):
        url = f"http://{host}/xbmcCmds/{form}?command=ExecBuiltIn(RunXBE({win_path}))"
        try:
            r = urllib.request.urlopen(url, timeout=6)
            print(f"HTTP launch via {form}: {r.status} {r.read(120)!r}")
            return
        except Exception as e:
            print(f"HTTP launch via {form} failed: {e}")
    print("=> HTTP auto-launch unavailable; launch E:\\Apps\\xlle-agent from the dashboard once.")

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="192.168.4.81")
    ap.add_argument("cmd")
    ap.add_argument("args", nargs="*")
    a = ap.parse_args()
    c = a.cmd.lower()
    if c == "ping":   print(agent_cmd(a.host, "PING"))
    elif c == "info": print(agent_cmd(a.host, "INFO"))
    elif c == "read": print(agent_cmd(a.host, f"R {a.args[0]} {a.args[1]}"))
    elif c == "write":print(agent_cmd(a.host, f"W {a.args[0]} {a.args[1]}"))
    elif c == "launch":print(agent_cmd(a.host, f"L {a.args[0]}"))
    elif c == "dash": print(agent_cmd(a.host, "DASH"))
    elif c == "exec":
        blob = open(a.args[0], "rb").read()
        print(agent_cmd(a.host, f"EXEC {len(blob)}", payload=blob))
    elif c == "deploy": deploy(a.host, a.args[0])
    else: sys.exit(f"unknown command: {a.cmd}")

if __name__ == "__main__":
    main()
