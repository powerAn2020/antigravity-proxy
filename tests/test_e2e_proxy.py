import socket
import threading
import sys
import os
import subprocess
import time

class MockSocks5Server:
    def __init__(self, tcp_port=17890, udp_port=17891):
        self.tcp_port = tcp_port
        self.udp_port = udp_port
        self.running = True
        self.tcp_server = None
        self.udp_sock = None
        self.intercepted_domains = []
        self.intercepted_udp_payloads = []

    def start(self, ready_event):
        self.tcp_server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.tcp_server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.tcp_server.bind(('127.0.0.1', self.tcp_port))
        self.tcp_server.listen(10)

        self.udp_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.udp_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.udp_sock.bind(('127.0.0.1', self.udp_port))
        self.udp_sock.settimeout(0.5)

        ready_event.set()

        udp_thread = threading.Thread(target=self._udp_loop, daemon=True)
        udp_thread.start()

        while self.running:
            try:
                self.tcp_server.settimeout(0.5)
                conn, addr = self.tcp_server.accept()
            except socket.timeout:
                continue
            except Exception:
                break
            threading.Thread(target=self._handle_tcp, args=(conn,), daemon=True).start()

    def _udp_loop(self):
        while self.running:
            try:
                data, client_addr = self.udp_sock.recvfrom(4096)
                if len(data) < 10:
                    continue
                atyp = data[3]
                header_len = 0
                target = ""
                port = 0
                if atyp == 1:
                    target = socket.inet_ntoa(data[4:8])
                    port = int.from_bytes(data[8:10], 'big')
                    header_len = 10
                elif atyp == 3:
                    dlen = data[4]
                    target = data[5:5+dlen].decode('utf-8', errors='ignore')
                    port = int.from_bytes(data[5+dlen:7+dlen], 'big')
                    header_len = 7 + dlen
                elif atyp == 4:
                    target = socket.inet_ntop(socket.AF_INET6, data[4:20])
                    port = int.from_bytes(data[20:22], 'big')
                    header_len = 22

                payload = data[header_len:]
                self.intercepted_udp_payloads.append((target, port, payload))
                print(f"[Mock SOCKS5 UDP] Intercepted UDP packet for {target}:{port}: {payload}")

                reply = data[:header_len] + b"EchoUDP: " + payload
                self.udp_sock.sendto(reply, client_addr)
            except socket.timeout:
                continue
            except Exception as e:
                if self.running:
                    print(f"[Mock SOCKS5 UDP] Error: {e}")

    def _handle_tcp(self, conn):
        try:
            conn.settimeout(5.0)
            req = conn.recv(1024)
            if not req:
                return
            conn.sendall(b'\x05\x00')

            cmd_req = conn.recv(1024)
            if len(cmd_req) < 4:
                return
            cmd = cmd_req[1]
            if cmd == 1:
                atyp = cmd_req[3]
                if atyp == 3:
                    dlen = cmd_req[4]
                    domain = cmd_req[5:5+dlen].decode('utf-8', errors='ignore')
                    port = int.from_bytes(cmd_req[5+dlen:7+dlen], 'big')
                    self.intercepted_domains.append((domain, port))
                    print(f"[Mock SOCKS5 TCP] Intercepted CONNECT domain: {domain}:{port}")
                elif atyp == 1:
                    ip = socket.inet_ntoa(cmd_req[4:8])
                    port = int.from_bytes(cmd_req[8:10], 'big')
                    print(f"[Mock SOCKS5 TCP] Intercepted CONNECT IPv4: {ip}:{port}")

                conn.sendall(b'\x05\x00\x00\x01\x7f\x00\x00\x01\x1f\x90')
                http_req = conn.recv(1024)
                conn.sendall(b"HTTP/1.1 200 OK\r\nContent-Length: 13\r\n\r\nHello Proxied")
                conn.close()

            elif cmd == 3:
                print(f"[Mock SOCKS5 TCP] Received UDP ASSOCIATE request")
                reply = b'\x05\x00\x00\x01' + socket.inet_aton('127.0.0.1') + (self.udp_port).to_bytes(2, 'big')
                conn.sendall(reply)
                while self.running:
                    buf = conn.recv(1024)
                    if not buf:
                        break
        except Exception:
            pass
        finally:
            try:
                conn.close()
            except Exception:
                pass

    def stop(self):
        self.running = False
        if self.tcp_server:
            try:
                self.tcp_server.close()
            except Exception:
                pass
        if self.udp_sock:
            try:
                self.udp_sock.close()
            except Exception:
                pass

if __name__ == '__main__':
    ready = threading.Event()
    tcp_port = 17890
    udp_port = 17891
    server = MockSocks5Server(tcp_port=tcp_port, udp_port=udp_port)
    t = threading.Thread(target=server.start, args=(ready,))
    t.daemon = True
    t.start()
    ready.wait()

    config_json = f'''{{
        "proxy": {{
            "host": "127.0.0.1",
            "port": {tcp_port},
            "type": "socks5"
        }},
        "fake_ip": {{
            "enabled": true,
            "cidr": "198.18.0.0/15"
        }},
        "proxy_rules": {{
            "allowed_ports": [80, 443, 8080],
            "udp_mode": "auto",
            "routing": {{
                "enabled": true,
                "rules": []
            }}
        }}
    }}'''

    cfg_path = "/tmp/test_proxy_config.json"
    with open(cfg_path, "w") as f:
        f.write(config_json)

    so_path = os.path.abspath("build_linux/libantigravity_proxy.so")
    env = os.environ.copy()
    for k in ["http_proxy", "https_proxy", "all_proxy", "HTTP_PROXY", "HTTPS_PROXY", "ALL_PROXY", "NO_PROXY", "no_proxy"]:
        env.pop(k, None)
    env["LD_PRELOAD"] = so_path
    env["ANTIGRAVITY_PROXY_CONFIG"] = cfg_path

    print("\n--- [Test 1] SOCKS5 TCP Connect via curl ---")
    cmd_tcp = ["curl", "-s", "-N", "http://my-fake-antigravity-domain.com:80/"]
    print(f"Executing: {' '.join(cmd_tcp)}")
    p_tcp = subprocess.Popen(cmd_tcp, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    stdout_tcp, stderr_tcp = p_tcp.communicate(timeout=5)
    print("Stdout:", stdout_tcp.decode('utf-8', errors='ignore'))
    assert b"Hello Proxied" in stdout_tcp, "Failed to receive proxied TCP response!"
    print("✅ TCP SOCKS5 Hooking & FakeIP test passed!")

    print("\n--- [Test 2] SOCKS5 UDP sendto/recvfrom via python ---")
    py_code_sendto = (
        "import socket\n"
        "s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)\n"
        "s.settimeout(3.0)\n"
        "s.sendto(b'PingSendTo', ('198.18.0.50', 8080))\n"
        "data, addr = s.recvfrom(1024)\n"
        "print('UDP_RESULT:', data.decode())\n"
        "s.close()\n"
    )
    p_udp1 = subprocess.Popen([sys.executable, "-c", py_code_sendto], env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    stdout_udp1, stderr_udp1 = p_udp1.communicate(timeout=5)
    print("Stdout:", stdout_udp1.decode('utf-8', errors='ignore'))
    if stderr_udp1:
        print("Stderr:", stderr_udp1.decode('utf-8', errors='ignore'))
    assert b"UDP_RESULT: EchoUDP: PingSendTo" in stdout_udp1, "Failed to receive proxied UDP sendto response!"
    print("✅ UDP SOCKS5 sendto/recvfrom test passed!")

    print("\n--- [Test 3] SOCKS5 UDP connect/send/recv via python ---")
    py_code_connect = (
        "import socket\n"
        "s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)\n"
        "s.settimeout(3.0)\n"
        "s.connect(('198.18.0.50', 8080))\n"
        "s.send(b'PingConnect')\n"
        "data = s.recv(1024)\n"
        "print('UDP_RESULT:', data.decode())\n"
        "s.close()\n"
    )
    p_udp2 = subprocess.Popen([sys.executable, "-c", py_code_connect], env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    stdout_udp2, stderr_udp2 = p_udp2.communicate(timeout=5)
    print("Stdout:", stdout_udp2.decode('utf-8', errors='ignore'))
    if stderr_udp2:
        print("Stderr:", stderr_udp2.decode('utf-8', errors='ignore'))
    assert b"UDP_RESULT: EchoUDP: PingConnect" in stdout_udp2, "Failed to receive proxied UDP connect/send response!"
    print("✅ UDP SOCKS5 connect/send/recv test passed!")

    print("\n--- [Test 4] Go Target (language_server) Proxy Environment Auto-Injection ---")
    import tempfile
    with tempfile.TemporaryDirectory() as tmpdir:
        parent_c = os.path.join(tmpdir, "parent.c")
        parent_bin = os.path.join(tmpdir, "parent")
        child_c = os.path.join(tmpdir, "child.c")
        child_bin = os.path.join(tmpdir, "language_server_linux_x64")

        with open(child_c, "w") as f:
            f.write('#include <stdio.h>\n#include <stdlib.h>\nint main() {\n    printf("CHILD_ALL_PROXY=%s\\n", getenv("ALL_PROXY"));\n    return 0;\n}\n')

        with open(parent_c, "w") as f:
            f.write('#include <unistd.h>\nint main() {\n    char* const argv[] = {(char*)"' + child_bin + '", NULL};\n    char* const envp[] = {NULL};\n    execve("' + child_bin + '", argv, envp);\n    return 0;\n}\n')

        subprocess.run(["gcc", child_c, "-o", child_bin], check=True)
        subprocess.run(["gcc", parent_c, "-o", parent_bin], check=True)

        p_ls = subprocess.run([parent_bin], env=env, capture_output=True, text=True)
        print("Stdout:", p_ls.stdout.strip())
        assert f"CHILD_ALL_PROXY=socks5://127.0.0.1:{tcp_port}" in p_ls.stdout, "Failed to auto-inject proxy into language_server!"
        print("✅ Go Target (language_server) Proxy Environment Auto-Injection passed!")

    server.stop()
    print("\n🎉 ALL E2E tests (TCP + UDP Associate + Go Env Injection) passed successfully!")
