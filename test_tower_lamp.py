"""PTY로 실제 장치 없이 C API의 전송·응답·상태 유지를 검증한다."""
import ctypes as C
import errno
import os
import select
import shlex
import subprocess
import tempfile
import threading
import time
from pathlib import Path


class State(C.Structure):
    _fields_ = [("color", C.c_int), ("buzzer", C.c_bool), ("blink", C.c_int)]


def main():
    root = Path(__file__).resolve().parent
    with tempfile.TemporaryDirectory() as tmp:
        library = str(Path(tmp) / "libtower_lamp.so")
        subprocess.run(shlex.split(os.environ.get("CC", "cc")) + [
            "-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic",
            "-shared", "-fPIC", str(root / "tower_lamp.c"), "-o", library,
        ], check=True)
        api = C.CDLL(library, use_errno=True)
        api.tower_lamp_open.argtypes = [C.c_char_p, C.c_uint, C.c_int]
        api.tower_lamp_open.restype = C.c_void_p
        api.tower_lamp_close.argtypes = [C.c_void_p]
        for name, argtype in [("apply", C.POINTER(State)), ("get_state", C.POINTER(State)),
                              ("set_color", C.c_int), ("set_buzzer", C.c_bool),
                              ("set_blink", C.c_int)]:
            getattr(api, "tower_lamp_" + name).argtypes = [C.c_void_p, argtype]
        api.tower_lamp_off.argtypes = [C.c_void_p]
        master, slave = os.openpty()
        lamp = api.tower_lamp_open(os.ttyname(slave).encode(), 9600, 150)
        assert lamp
        try:
            assert api.tower_lamp_set_buzzer(lamp, True) == -1
            assert C.get_errno() == errno.ENODATA
            assert not select.select([master], [], [], 0)[0]

            def transaction(call, expected, reply=True):
                errors = []
                def device():
                    try:
                        received = b""
                        while len(received) < 5:
                            assert select.select([master], [], [], 1)[0], "missing command"
                            received += os.read(master, 5 - len(received))
                        assert received == bytes.fromhex(expected), received.hex()
                        if reply:
                            os.write(master, b"noise" + received[:2])
                            time.sleep(0.01)
                            os.write(master, received[2:])
                        else:
                            os.write(master, bytes.fromhex("FF 04 02 04 AA"))
                    except BaseException as exc:
                        errors.append(exc)
                worker = threading.Thread(target=device)
                worker.start()
                result = call()
                error = C.get_errno()
                worker.join()
                if errors:
                    raise errors[0]
                assert result == (0 if reply else -1)
                if not reply:
                    assert error == errno.ETIMEDOUT, error

            transaction(lambda: api.tower_lamp_off(lamp), "FF 01 01 01 AA")
            transaction(lambda: api.tower_lamp_set_buzzer(lamp, True), "FF 01 02 01 AA")
            transaction(lambda: api.tower_lamp_set_color(lamp, 4), "FF 04 02 01 AA")
            transaction(lambda: api.tower_lamp_set_blink(lamp, 3), "FF 04 02 03 AA")
            transaction(lambda: api.tower_lamp_set_buzzer(lamp, False), "FF 04 01 03 AA")
            state = State()
            assert api.tower_lamp_get_state(lamp, C.byref(state)) == 0
            assert (state.color, state.buzzer, state.blink) == (4, False, 3)
            assert api.tower_lamp_set_color(lamp, 99) == -1
            assert C.get_errno() == errno.EINVAL
            assert api.tower_lamp_get_state(lamp, C.byref(state)) == 0
            transaction(lambda: api.tower_lamp_set_buzzer(lamp, True), "FF 04 02 03 AA", False)
            assert api.tower_lamp_get_state(lamp, C.byref(state)) == -1
            assert C.get_errno() == errno.ENODATA
            transaction(lambda: api.tower_lamp_off(lamp), "FF 01 01 01 AA")
        finally:
            api.tower_lamp_close(lamp)
            os.close(master)
            os.close(slave)
    print("PASS: strict C build, independent controls, fragmented echo, validation, timeout, recovery")


if __name__ == "__main__":
    main()
