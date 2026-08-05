#!/usr/bin/env python3
"""Classify same interface:port behavior as rejected, virtual-hosted, or broken."""

import argparse
import http.client
import os
import signal
import socket
import subprocess
import sys
import time
from pathlib import Path


def port_open(host: str, port: int) -> bool:
    try:
        with socket.create_connection((host, port), timeout=0.2):
            return True
    except OSError:
        return False


def request(host: str, port: int, host_header: str) -> tuple[int, bytes]:
    conn = http.client.HTTPConnection(host, port, timeout=3)
    conn.request("GET", "/", headers={"Host": host_header, "Connection": "close"})
    response = conn.getresponse()
    body = response.read()
    status = response.status
    conn.close()
    return status, body


def stop_process(process: subprocess.Popen) -> None:
    if process.poll() is not None:
        return
    try:
        process.send_signal(signal.SIGINT)
        process.wait(timeout=3)
        return
    except subprocess.TimeoutExpired:
        pass
    try:
        process.terminate()
        process.wait(timeout=2)
        return
    except subprocess.TimeoutExpired:
        pass
    process.kill()
    process.wait(timeout=2)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument("--cwd", required=True)
    parser.add_argument("--config", required=True)
    parser.add_argument("--host", required=True)
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--log", required=True)
    args = parser.parse_args()

    Path(args.log).parent.mkdir(parents=True, exist_ok=True)
    with open(args.log, "wb") as log:
        process = subprocess.Popen(
            [args.binary, args.config],
            cwd=args.cwd,
            stdout=log,
            stderr=subprocess.STDOUT,
        )
        try:
            listening = False
            for _ in range(40):
                if process.poll() is not None:
                    break
                if port_open(args.host, args.port):
                    listening = True
                    break
                time.sleep(0.1)

            if not listening:
                print("RESULT=REJECTED")
                return 0

            alpha_status, alpha_body = request(args.host, args.port, "alpha.test")
            beta_status, beta_body = request(args.host, args.port, "beta.test")

            alpha_ok = alpha_status == 200 and b"HOST_ALPHA_OK" in alpha_body
            beta_ok = beta_status == 200 and b"HOST_BETA_OK" in beta_body
            if alpha_ok and beta_ok:
                print("RESULT=VHOST")
                return 0

            print("RESULT=BROKEN")
            print(f"ALPHA_STATUS={alpha_status}")
            print(f"BETA_STATUS={beta_status}")
            print("ALPHA_BODY=" + alpha_body.decode("latin1", "replace")[:200])
            print("BETA_BODY=" + beta_body.decode("latin1", "replace")[:200])
            return 1
        finally:
            stop_process(process)


if __name__ == "__main__":
    raise SystemExit(main())
