#!/usr/bin/env python3
"""Send a raw HTTP request to Webserv and save one complete HTTP response."""

import argparse
import socket
import sys
import time
from pathlib import Path


def split_headers(data: bytes):
    if b"\r\n\r\n" in data:
        head, body = data.split(b"\r\n\r\n", 1)
        return head, body, 4
    if b"\n\n" in data:
        head, body = data.split(b"\n\n", 1)
        return head, body, 2
    return None, None, 0


def response_complete(data: bytes, request_method: bytes) -> bool:
    head, body, _ = split_headers(data)
    if head is None:
        return False

    lines = head.replace(b"\r\n", b"\n").split(b"\n")
    status = 0
    if lines:
        parts = lines[0].split()
        if len(parts) >= 2:
            try:
                status = int(parts[1])
            except ValueError:
                status = 0

    if request_method == b"HEAD" or 100 <= status < 200 or status in (204, 304):
        return True

    content_length = None
    for line in lines[1:]:
        if b":" not in line:
            continue
        name, value = line.split(b":", 1)
        if name.strip().lower() == b"content-length":
            try:
                content_length = int(value.strip())
            except ValueError:
                content_length = None
            break

    if content_length is not None:
        return len(body) >= content_length
    return False


def receive_response(sock: socket.socket, timeout: float, request_method: bytes) -> bytes:
    chunks = []
    data = b""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        sock.settimeout(max(0.05, deadline - time.monotonic()))
        try:
            chunk = sock.recv(65536)
        except socket.timeout:
            break
        if not chunk:
            break
        chunks.append(chunk)
        data += chunk
        if response_complete(data, request_method):
            break
    return b"".join(chunks)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--request-file", required=True)
    parser.add_argument("--response-file", required=True)
    parser.add_argument("--timeout", type=float, default=2.0)
    parser.add_argument("--fragment-delay", type=float, default=0.0)
    parser.add_argument(
        "--separator",
        default="<FRAGMENT>",
        help="Split request bytes at this ASCII marker when fragment-delay is used.",
    )
    args = parser.parse_args()

    request = Path(args.request_file).read_bytes()
    request_method = request.split(None, 1)[0].upper() if request.split(None, 1) else b""
    parts = [request]
    marker = args.separator.encode("ascii")
    if args.fragment_delay > 0 and marker in request:
        parts = request.split(marker)

    try:
        with socket.create_connection((args.host, args.port), timeout=args.timeout) as sock:
            for index, part in enumerate(parts):
                if part:
                    try:
                        sock.sendall(part)
                    except BrokenPipeError:
                        break
                if index + 1 < len(parts):
                    time.sleep(args.fragment_delay)
            # 不调用 shutdown(SHUT_WR)：有的实现会把半关闭立即当作客户端离线并清理连接。
            response = receive_response(sock, args.timeout, request_method)
    except OSError as error:
        print(f"raw_http.py: {error}", file=sys.stderr)
        return 1

    Path(args.response_file).write_bytes(response)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())