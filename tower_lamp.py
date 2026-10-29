#!/usr/bin/env python3
"""첨부된 5바이트 프로토콜을 사용하는 Linux 시리얼 타워 램프 제어."""

import argparse
import fcntl
import os
import select
import termios
import time
import tty


MODES = {"off": 1, "green": 2, "yellow": 3, "red": 4}


def send(port, baud, packet, timeout):
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    original = None
    try:
        fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        original = termios.tcgetattr(fd)
        tty.setraw(fd)
        attrs = termios.tcgetattr(fd)
        attrs[2] &= ~(termios.PARENB | termios.CSTOPB | termios.CSIZE | termios.CRTSCTS)
        attrs[2] |= termios.CS8 | termios.CREAD | termios.CLOCAL
        attrs[4] = attrs[5] = getattr(termios, f"B{baud}")
        termios.tcsetattr(fd, termios.TCSANOW, attrs)
        termios.tcflush(fd, termios.TCIFLUSH)
        written = os.write(fd, packet)
        if written != len(packet):
            raise OSError(f"일부 바이트만 전송됨: {written}/{len(packet)}")
        termios.tcdrain(fd)
        print(f"TX {packet.hex(' ').upper()}", flush=True)
        response = bytearray()
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            ready, _, _ = select.select([fd], [], [], max(0, deadline - time.monotonic()))
            if ready:
                chunk = os.read(fd, 256)
                if not chunk:
                    break
                response.extend(chunk)
                if packet in response:
                    print(f"RX {response.hex(' ').upper()} — 명령 일치 확인")
                    return True
        print(f"RX {response.hex(' ').upper() or '(응답 없음)'} — 동작 확인 필요")
        return False
    finally:
        try:
            if original is not None:
                termios.tcsetattr(fd, termios.TCSANOW, original)
        finally:
            os.close(fd)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("color", choices=MODES)
    parser.add_argument("--port", default="/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0")
    parser.add_argument("--baud", type=int, choices=[1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200], default=9600)
    parser.add_argument("--buzzer", action="store_true", help="부저 켜기 (기본: 꺼짐)")
    parser.add_argument("--blink", type=int, choices=[1, 2, 3, 4], default=1, help="1: 점멸 끄기, 2~4: 표의 점멸 모드")
    parser.add_argument("--timeout", type=float, default=1.5)
    parser.add_argument("--dry-run", action="store_true", help="장치에 전송하지 않고 명령 출력")
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error("--timeout은 양수여야 합니다")
    packet = bytes([0xFF, MODES[args.color], 2 if args.buzzer else 1, args.blink, 0xAA])
    if args.dry_run:
        print(packet.hex(' ').upper())
        return 0
    try:
        return 0 if send(args.port, args.baud, packet, args.timeout) else 1
    except (OSError, termios.error) as exc:
        parser.exit(2, f"장치 통신 오류: {exc}\n")


if __name__ == "__main__":
    raise SystemExit(main())
