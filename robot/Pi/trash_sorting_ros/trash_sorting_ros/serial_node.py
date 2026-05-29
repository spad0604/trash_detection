import threading
import time
from typing import Callable, Optional


try:
    import serial
except ImportError:  # pragma: no cover - handled at runtime on the Pi
    serial = None


class SerialWorker:
    def __init__(
        self,
        *,
        port: str,
        baudrate: int,
        timeout: float,
        reconnect_delay: float,
        on_line: Callable[[str], None],
        on_error: Callable[[str], None],
        on_connect: Callable[[str], None],
    ) -> None:
        self.port = port
        self.baudrate = baudrate
        self.timeout = timeout
        self.reconnect_delay = reconnect_delay
        self.on_line = on_line
        self.on_error = on_error
        self.on_connect = on_connect
        self._serial: Optional["serial.Serial"] = None
        self._stop = threading.Event()
        self._lock = threading.Lock()
        self._thread = threading.Thread(target=self._run, daemon=True)

    def start(self) -> None:
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()
        with self._lock:
            if self._serial is not None:
                try:
                    self._serial.close()
                except Exception:
                    pass

    def write_line(self, line: str) -> bool:
        payload = (line.strip() + "\n").encode("utf-8")
        with self._lock:
            if self._serial is None or not self._serial.is_open:
                return False
            self._serial.write(payload)
            self._serial.flush()
            return True

    def _run(self) -> None:
        if serial is None:
            self.on_error("pyserial is not installed")
            return

        while not self._stop.is_set():
            try:
                with self._lock:
                    self._serial = serial.Serial(
                        self.port,
                        self.baudrate,
                        timeout=self.timeout,
                        write_timeout=self.timeout,
                    )
                    try:
                        self._serial.dtr = False
                        self._serial.rts = False
                    except Exception:
                        pass
                self.on_connect(self.port)
                self._read_loop()
            except Exception as exc:
                self.on_error(f"{self.port}: {exc}")
            finally:
                with self._lock:
                    if self._serial is not None:
                        try:
                            self._serial.close()
                        except Exception:
                            pass
                    self._serial = None

            if not self._stop.wait(self.reconnect_delay):
                continue

    def _read_loop(self) -> None:
        while not self._stop.is_set():
            with self._lock:
                ser = self._serial
            if ser is None or not ser.is_open:
                return

            raw = ser.readline()
            if not raw:
                time.sleep(0.01)
                continue

            line = raw.decode("utf-8", errors="replace").strip()
            if line:
                self.on_line(line)
