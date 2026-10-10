import socket
import threading
import sys
import os
import subprocess

def run_socks5_server(port, ready_event):
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(('127.0.0.1', port))
    server.listen(5)
    ready_event.set()

    conn, addr = server.accept()
    # 1. Auth negotiation
    req = conn.recv(3)
    assert req == b'\x05\x01\x00', f"Unexpected auth req: {req}"
    conn.sendall(b'\x05\x00')

    # 2. Connect request
    conn_req = conn.recv(1024)
    # VER CMD RSV ATYP ...
    assert conn_req[0] == 5 and conn_req[1] == 1, f"Unexpected cmd: {conn_req}"
    atyp = conn_req[3]
    if atyp == 3: # Domain
        domain_len = conn_req[4]
        domain = conn_req[5:5+domain_len].decode('utf-8')
        target_port = int.from_bytes(conn_req[5+domain_len:7+domain_len], 'big')
        print(f"[Mock SOCKS5] Successfully intercepted request for domain: {domain}:{target_port}")
    elif atyp == 1: # IPv4
        ip = socket.inet_ntoa(conn_req[4:8])
        target_port = int.from_bytes(conn_req[8:10], 'big')
        print(f"[Mock SOCKS5] Intercepted request for IPv4: {ip}:{target_port}")

    # Reply success
    conn.sendall(b'\x05\x00\x00\x01\x7f\x00\x00\x01\x1f\x90')
    # Echo back
    data = conn.recv(1024)
    conn.sendall(b"HTTP/1.1 200 OK\r\nContent-Length: 13\r\n\r\nHello Proxied")
    conn.close()
    server.close()

if __name__ == '__main__':
    ready = threading.Event()
    port = 17890
    t = threading.Thread(target=run_socks5_server, args=(port, ready))
    t.daemon = True
    t.start()
    ready.wait()

    # Create config.json with this proxy port
    config_json = f'''{{
        "proxy": {{
            "host": "127.0.0.1",
            "port": {port},
            "type": "socks5"
        }},
        "fake_ip": {{
            "enabled": true,
            "cidr": "198.18.0.0/15"
        }},
        "proxy_rules": {{
            "allowed_ports": [80, 443, 8080],
            "routing": {{
                "enabled": true,
                "rules": []
            }}
        }}
    }}'''

    cfg_path = "/tmp/test_proxy_config.json"
    with open(cfg_path, "w") as f:
        f.write(config_json)

    # Now run curl using LD_PRELOAD
    so_path = os.path.abspath("build_linux/libantigravity_proxy.so")
    env = os.environ.copy()
    env["LD_PRELOAD"] = so_path
    env["ANTIGRAVITY_PROXY_CONFIG"] = cfg_path

    cmd = ["curl", "-s", "-N", "http://my-fake-antigravity-domain.com:80/"]
    print(f"Executing: {' '.join(cmd)}")
    p = subprocess.Popen(cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    stdout, stderr = p.communicate(timeout=5)
    print("Stdout:", stdout.decode('utf-8', errors='ignore'))
    print("Stderr:", stderr.decode('utf-8', errors='ignore'))
    t.join(timeout=2)
    assert b"Hello Proxied" in stdout, "Failed to receive proxied response!"
    print("🎉 SOCKS5 Hooking & FakeIP test passed!")
