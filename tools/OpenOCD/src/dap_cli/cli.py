from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import os
import re
import socket
import statistics
import struct
import subprocess
import sys
import threading
import time
from collections import deque
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable


TOOL_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_CONFIG = TOOL_ROOT / "dap.json"
TCL_SEPARATOR = b"\x1a"


class DapError(RuntimeError):
    pass


def _json_dump(value: Any) -> str:
    return json.dumps(value, ensure_ascii=False, separators=(",", ":"))


def _split_names(values: Iterable[str]) -> list[str]:
    names: list[str] = []
    for value in values:
        names.extend(x for x in re.split(r"[,\s]+", value.strip()) if x)
    return list(dict.fromkeys(names))


def _resolved_path(base: Path, value: str) -> Path:
    path = Path(os.path.expandvars(os.path.expanduser(value)))
    return path if path.is_absolute() else (base / path).resolve()


@dataclass
class Settings:
    path: Path
    host: str
    port: int
    elf: Path
    liveness_symbol: str | None
    aliases: dict[str, str]
    safe_rates: dict[str, int]
    openocd_executable: Path | None
    openocd_scripts: Path | None
    openocd_config: Path
    writable: list[str]
    write_limits: dict[str, tuple[float, float]]
    estop_symbol: str | None

    @classmethod
    def load(cls, path: Path) -> "Settings":
        try:
            raw = json.loads(path.read_text(encoding="utf-8"))
        except FileNotFoundError as exc:
            raise DapError(f"配置文件不存在: {path}") from exc
        except json.JSONDecodeError as exc:
            raise DapError(f"配置文件不是有效 JSON: {path}: {exc}") from exc
        base = path.parent
        openocd = raw.get("openocd", {})
        exe = openocd.get("executable")
        scripts = openocd.get("scripts")
        return cls(
            path=path,
            host=str(raw.get("host", "127.0.0.1")),
            port=int(raw.get("port", 6666)),
            elf=_resolved_path(base, raw["elf"]),
            liveness_symbol=str(raw["liveness_symbol"]) if raw.get("liveness_symbol") else None,
            aliases={str(k): str(v) for k, v in raw.get("aliases", {}).items()},
            safe_rates={"1-4": 50, "5-8": 25, "9+": 10, **raw.get("safe_rates_hz", {})},
            openocd_executable=_resolved_path(base, exe) if exe else None,
            openocd_scripts=_resolved_path(base, scripts) if scripts else None,
            openocd_config=_resolved_path(base, openocd.get("config", "openocd.cfg")),
            writable=[str(name) for name in raw.get("writable", [])],
            write_limits={
                str(name): (int(limit[0]), int(limit[1]))
                for name, limit in raw.get("write_limits", {}).items()
            },
            estop_symbol=str(raw["estop_symbol"]) if raw.get("estop_symbol") else None,
        )

    def safe_rate(self, count: int) -> int:
        if count <= 4:
            return int(self.safe_rates["1-4"])
        if count <= 8:
            return int(self.safe_rates["5-8"])
        return int(self.safe_rates["9+"])


class TclClient:
    """Minimal OpenOCD Tcl-RPC client. It exposes reads only."""

    def __init__(self, host: str, port: int, timeout: float = 3.0):
        self.host = host
        self.port = port
        self.timeout = timeout
        self.sock: socket.socket | None = None

    def connect(self) -> None:
        self.close()
        sock = socket.create_connection((self.host, self.port), timeout=self.timeout)
        sock.settimeout(self.timeout)
        self.sock = sock

    def close(self) -> None:
        if self.sock is not None:
            try:
                self.sock.close()
            finally:
                self.sock = None

    def tcl(self, script: str) -> str:
        if self.sock is None:
            self.connect()
        assert self.sock is not None
        self.sock.sendall(script.encode("utf-8") + TCL_SEPARATOR)
        chunks = bytearray()
        while TCL_SEPARATOR not in chunks:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise DapError("OpenOCD Tcl 连接意外关闭")
            chunks.extend(chunk)
        return bytes(chunks).split(TCL_SEPARATOR, 1)[0].decode("utf-8", "replace").strip()

    def command(self, command: str) -> str:
        return self.tcl("capture {" + command + "}")

    def __enter__(self) -> "TclClient":
        self.connect()
        return self

    def __exit__(self, *_: object) -> None:
        self.close()


@dataclass
class Variable:
    requested: str
    target: str
    address: int
    size: int
    node: Any


class ElfVariables:
    def __init__(self, elf: Path, aliases: dict[str, str]):
        if not elf.is_file():
            raise DapError(f"ELF 不存在: {elf}")
        try:
            from openocd_mcp.dwarf_types import TypeResolver
            from openocd_mcp.symbols import SymbolTable
        except ImportError as exc:
            raise DapError("缺少 openocd-mcp/pyelftools，请安装 requirements/requirements-mcp.txt") from exc
        table = SymbolTable()
        table.load(str(elf))
        self.symbols = dict(table._syms)
        self.resolver = TypeResolver(str(elf))
        self.elf = elf
        self.aliases = aliases

    def find(self, pattern: str) -> list[str]:
        key = pattern.lower()
        return sorted(name for name in self.symbols if key in name.lower())

    def resolve(self, requested: str) -> Variable:
        target = self.aliases.get(requested, requested)
        parts = target.split(".")
        root = parts[0]
        symbol = self.symbols.get(root)
        if symbol is None:
            hint = self.find(root)[:5]
            suffix = f"；相近符号: {', '.join(hint)}" if hint else ""
            raise DapError(f"ELF 中没有变量或别名 {requested!r}（解析为 {target!r}）{suffix}")
        address, symbol_size = symbol
        node = self.resolver.resolve(root)
        if node is None:
            node = type("Scalar", (), {
                "name": root, "offset": 0, "size": symbol_size or 4,
                "kind": "base", "encoding": "unsigned", "children": [],
                "type_name": "unknown", "bit_size": None, "bit_offset": None,
                "enum_values": None,
            })()
        for member in parts[1:]:
            match = next((child for child in node.children if child.name == member), None)
            if match is None:
                raise DapError(f"{target!r} 中没有成员 {member!r}")
            node = match
        if getattr(node, "children", None):
            raise DapError(f"{requested!r} 是聚合类型，请指定叶子成员")
        size = int(node.size or symbol_size or 4)
        if size not in (1, 2, 4, 8):
            raise DapError(f"{requested!r} 的 {size} 字节类型暂不支持")
        return Variable(requested, target, int(address) + int(node.offset), size, node)


def _decode(variable: Variable, raw: bytes) -> int | float | bool:
    node = variable.node
    if node.kind == "base" and node.encoding == "float":
        if variable.size == 4:
            return struct.unpack("<f", raw)[0]
        if variable.size == 8:
            return struct.unpack("<d", raw)[0]
    value = int.from_bytes(raw, "little", signed=False)
    if node.bit_size:
        value = (value >> (node.bit_offset or 0)) & ((1 << node.bit_size) - 1)
    if node.encoding == "signed":
        bits = node.bit_size or variable.size * 8
        if value >= (1 << (bits - 1)):
            value -= 1 << bits
    if node.encoding == "bool":
        return bool(value)
    return value


def _encode(variable: Variable, value: int | float) -> bytes:
    """_decode 的对称实现：按 DWARF 类型把数值编成小端字节。"""
    node = variable.node
    if node.kind == "base" and node.encoding == "float":
        if variable.size == 4:
            return struct.pack("<f", float(value))
        if variable.size == 8:
            return struct.pack("<d", float(value))
        raise DapError(f"{variable.requested!r} 的 {variable.size} 字节浮点类型暂不支持写入")
    if node.bit_size:
        raise DapError(
            f"{variable.requested!r} 是位域（{node.bit_size} 位）；"
            "不支持位域写入，避免读改写破坏相邻位"
        )
    signed = node.encoding == "signed"
    return int(value).to_bytes(variable.size, "little", signed=signed)


_MEMORY_LINE = re.compile(r"^\s*(0x[0-9a-fA-F]+):\s+(.+)$")


def read_variables(client: TclClient, variables: list[Variable]) -> dict[str, int | float | bool]:
    # Merge nearby symbols into bounded contiguous blocks. This reduces both
    # wireless round trips and CMSIS-DAP commands without reading a large,
    # unrelated RAM region. The core remains running throughout.
    blocks: list[list[int]] = []
    for variable in sorted(variables, key=lambda item: item.address):
        end = variable.address + variable.size
        if blocks and end - blocks[-1][0] <= 256:
            blocks[-1][1] = max(blocks[-1][1], end)
        else:
            blocks.append([variable.address, end])
    commands = [f"mdb 0x{start:x} {end - start}" for start, end in blocks]
    if len(commands) == 1:
        output = client.command(commands[0])
    else:
        captures = " ".join(f"[capture {{{command}}}]" for command in commands)
        output = client.tcl(f"join [list {captures}] \"\\n\"")
    memory: dict[int, int] = {}
    for line in output.splitlines():
        match = _MEMORY_LINE.match(line)
        if not match:
            continue
        address = int(match.group(1), 16)
        for offset, token in enumerate(match.group(2).split()):
            if re.fullmatch(r"[0-9a-fA-F]{2}", token):
                memory[address + offset] = int(token, 16)
    result: dict[str, int | float | bool] = {}
    for variable in variables:
        try:
            raw = bytes(memory[variable.address + i] for i in range(variable.size))
        except KeyError as exc:
            raise DapError(f"没有读到 {variable.requested} @ 0x{variable.address:08X}") from exc
        result[variable.requested] = _decode(variable, raw)
    return result


def _target_running(text: str) -> bool:
    return bool(re.search(r"\brunning\b", text, re.IGNORECASE))


def _sample_rows(client: TclClient, variables: list[Variable], rate: float,
                 duration: float | None, stop: threading.Event | None = None):
    period = 1.0 / rate
    started = time.perf_counter()
    deadline = started
    sequence = 0
    misses = 0
    while stop is None or not stop.is_set():
        now = time.perf_counter()
        if duration is not None and now - started >= duration:
            break
        if now < deadline:
            time.sleep(deadline - now)
        read_started = time.perf_counter()
        values = read_variables(client, variables)
        finished = time.perf_counter()
        sequence += 1
        deadline += period
        if finished > deadline:
            skipped = int((finished - deadline) // period) + 1
            misses += skipped
            deadline += skipped * period
        yield {
            "seq": sequence,
            "elapsed_s": round(finished - started, 6),
            "read_ms": round((finished - read_started) * 1000.0, 3),
            "deadline_misses": misses,
            "values": values,
        }
        if duration is not None and deadline - started >= duration:
            break


def _rate_for(args: argparse.Namespace, settings: Settings, count: int) -> float:
    safe = settings.safe_rate(count)
    rate = float(args.rate if args.rate is not None else safe)
    if rate <= 0:
        raise DapError("--rate 必须大于 0")
    if rate > safe and not args.force:
        raise DapError(
            f"{count} 个分散变量的实测保守上限是 {safe} Hz；"
            f"如确需压力测试，请显式增加 --force"
        )
    return rate


def _variables(settings: Settings, names: list[str]) -> tuple[ElfVariables, list[Variable]]:
    if not names:
        raise DapError("至少指定一个变量")
    index = ElfVariables(settings.elf, settings.aliases)
    return index, [index.resolve(name) for name in names]


def cmd_doctor(args: argparse.Namespace, settings: Settings) -> int:
    report: dict[str, Any] = {
        "ok": False,
        "host": settings.host,
        "port": settings.port,
        "elf": str(settings.elf),
        "elf_exists": settings.elf.is_file(),
        "config": str(settings.path),
    }
    index: ElfVariables | None = None
    if settings.elf.is_file():
        report["elf_sha256"] = hashlib.sha256(settings.elf.read_bytes()).hexdigest()
        try:
            index = ElfVariables(settings.elf, settings.aliases)
            report["symbol_count"] = len(index.symbols)
        except DapError as exc:
            report["elf_error"] = str(exc)
    try:
        with TclClient(settings.host, settings.port) as client:
            state = client.command("targets")
            liveness_ok: bool | None = None
            if _target_running(state) and settings.liveness_symbol and index is not None:
                probe = index.resolve(settings.liveness_symbol)
                first = read_variables(client, [probe])[settings.liveness_symbol]
                time.sleep(0.05)
                second = read_variables(client, [probe])[settings.liveness_symbol]
                liveness_ok = isinstance(first, (int, float)) and isinstance(second, (int, float)) and second != first
                report["liveness"] = {
                    "symbol": settings.liveness_symbol,
                    "first": first,
                    "second": second,
                    "advanced": liveness_ok,
                }
        report.update(openocd_connected=True, target_running=_target_running(state), target=state)
        report["ok"] = bool(
            report["elf_exists"] and report["target_running"]
            and (liveness_ok is not False)
        )
        if liveness_ok is False:
            report["error"] = "活性变量未递增：可能是 ELF/板上固件不匹配，或目标时基未运行"
    except (OSError, DapError) as exc:
        report.update(openocd_connected=False, target_running=False, error=str(exc))
    if args.json:
        print(_json_dump(report))
    else:
        for key, value in report.items():
            print(f"{key}: {value}")
    return 0 if report["ok"] else 2


def cmd_symbols(args: argparse.Namespace, settings: Settings) -> int:
    index = ElfVariables(settings.elf, settings.aliases)
    names = index.find(args.filter)
    if args.json:
        print(_json_dump({"count": len(names), "symbols": names}))
    else:
        print("\n".join(names))
    return 0


def cmd_read(args: argparse.Namespace, settings: Settings) -> int:
    names = _split_names(args.names)
    _, variables = _variables(settings, names)
    with TclClient(settings.host, settings.port) as client:
        before = client.command("targets")
        if not _target_running(before):
            raise DapError("目标当前不是 running；为避免改变程序状态，dap 不会自动 resume")
        started = time.perf_counter()
        values = read_variables(client, variables)
        read_ms = (time.perf_counter() - started) * 1000.0
    payload = {
        "ok": True,
        "target_running": True,
        "read_ms": round(read_ms, 3),
        "elf": str(settings.elf),
        "values": values,
    }
    if args.json:
        print(_json_dump(payload))
    else:
        for name, value in values.items():
            print(f"{name} = {value}")
    return 0


_WRITE_SIZE_COMMAND = {1: "mwb", 2: "mwh", 4: "mww"}


def cmd_write(args: argparse.Namespace, settings: Settings) -> int:
    """白名单写参：先写后读回验证。限幅在配置层强制，双保险由固件兜底。"""
    if not settings.writable:
        raise DapError("dap.json 未配置 writable 白名单，拒绝一切写入")
    if args.name not in settings.writable:
        raise DapError(
            f"{args.name!r} 不在 writable 白名单内: {', '.join(settings.writable)}"
        )
    index = ElfVariables(settings.elf, settings.aliases)
    variable = index.resolve(args.name)
    # 先按 DWARF 类型确定解析方式，再做限幅与编码，避免把浮点串当整数处理。
    is_float = variable.node.kind == "base" and variable.node.encoding == "float"
    if is_float:
        value: int | float = float(args.value)
    else:
        value = int(args.value, 0)
    limits = settings.write_limits.get(args.name)
    if limits is not None and not (limits[0] <= value <= limits[1]):
        raise DapError(f"{args.name} 允许范围 {limits[0]}..{limits[1]}，收到 {value}")
    if variable.size not in _WRITE_SIZE_COMMAND:
        raise DapError(f"{args.name} 的 {variable.size} 字节类型暂不支持写入")
    started = time.perf_counter()
    with TclClient(settings.host, settings.port) as client:
        state = client.command("targets")
        if not _target_running(state):
            raise DapError("目标当前不是 running；dap 不会在非运行态写参（急停请用 estop）")
        raw = _encode(variable, value)
        word = int.from_bytes(raw, "little", signed=False)
        client.command(f"{_WRITE_SIZE_COMMAND[variable.size]} 0x{variable.address:x} 0x{word:x}")
        time.sleep(0.05)
        values = read_variables(client, [variable])
    written = values[args.name]
    if is_float:
        # float32 无法精确表示所有十进制，用相对容差比对读回值。
        ok = bool(abs(written - value) <= 1e-6 * max(1.0, abs(value)))
    else:
        ok = bool(written == value)
    payload = {
        "ok": ok,
        "name": args.name,
        "requested": value,
        "readback": written,
        "address": f"0x{variable.address:08X}",
        "write_ms": round((time.perf_counter() - started) * 1000.0, 3),
    }
    if args.json:
        print(_json_dump(payload))
    else:
        print("\n".join(f"{k}: {v}" for k, v in payload.items()))
    return 0 if ok else 2


def cmd_estop(args: argparse.Namespace, settings: Settings) -> int:
    """急停：写 estop_symbol=0。不检查 running——目标 halted 时也执行，
    避免 resume 后带着残油门失控。不等于物理断电。"""
    if not settings.estop_symbol:
        raise DapError("dap.json 未配置 estop_symbol")
    index = ElfVariables(settings.elf, settings.aliases)
    variable = index.resolve(settings.estop_symbol)
    if variable.size != 1:
        raise DapError(
            f"estop_symbol {settings.estop_symbol} 宽度为 {variable.size} 字节；"
            "estop 只按 1 字节写入，请修正 dap.json 的 estop_symbol"
        )
    started = time.perf_counter()
    with TclClient(settings.host, settings.port) as client:
        client.command(f"mwb 0x{variable.address:x} 0")
        values = read_variables(client, [variable])
    written = values[settings.estop_symbol]
    ok = bool(written in (0, False))
    payload = {
        "ok": ok,
        "name": settings.estop_symbol,
        "readback": written,
        "address": f"0x{variable.address:08X}",
        "estop_ms": round((time.perf_counter() - started) * 1000.0, 3),
    }
    if args.json:
        print(_json_dump(payload))
    else:
        print("\n".join(f"{k}: {v}" for k, v in payload.items()))
    return 0 if ok else 2


def _headless_monitor(args: argparse.Namespace, settings: Settings,
                      variables: list[Variable], rate: float) -> int:
    duration = args.duration
    if duration is None:
        raise DapError("无图形 monitor 必须给 --duration，避免无人值守无限占用探针")
    with TclClient(settings.host, settings.port) as client:
        state = client.command("targets")
        if not _target_running(state):
            raise DapError("目标当前不是 running；dap 不会改变目标运行状态")
        for row in _sample_rows(client, variables, rate, duration):
            print(_json_dump(row), flush=True)
    return 0


def _configure_tk_runtime() -> None:
    """Repair venv Tk lookup on Windows embeddable/per-user Python installs."""
    if os.name != "nt":
        return
    tcl_root = Path(sys.base_prefix) / "tcl"
    if not tcl_root.is_dir():
        return
    tcl_dirs = sorted(tcl_root.glob("tcl8.*"), reverse=True)
    tk_dirs = sorted(tcl_root.glob("tk8.*"), reverse=True)
    if tcl_dirs:
        os.environ.setdefault("TCL_LIBRARY", str(tcl_dirs[0]))
    if tk_dirs:
        os.environ.setdefault("TK_LIBRARY", str(tk_dirs[0]))


def _gui_monitor(args: argparse.Namespace, settings: Settings,
                 variables: list[Variable], rate: float) -> int:
    _configure_tk_runtime()
    try:
        import matplotlib.pyplot as plt
        from matplotlib.animation import FuncAnimation
    except ImportError as exc:
        raise DapError("图形模式需要 matplotlib") from exc

    stop = threading.Event()
    rows: deque[dict[str, Any]] = deque(maxlen=max(200, int(rate * args.window * 2)))
    failure: list[str] = []

    def acquire() -> None:
        try:
            with TclClient(settings.host, settings.port) as client:
                state = client.command("targets")
                if not _target_running(state):
                    raise DapError("目标当前不是 running；dap 不会改变目标运行状态")
                for row in _sample_rows(client, variables, rate, args.duration, stop):
                    rows.append(row)
        except Exception as exc:  # surfaced in the GUI status and on exit
            failure.append(str(exc))
        finally:
            stop.set()

    worker = threading.Thread(target=acquire, name="dap-sampler", daemon=True)
    worker.start()
    figure, axes = plt.subplots(len(variables), 1, sharex=True, squeeze=False,
                               figsize=(10, max(3.5, 2.2 * len(variables))))
    flat_axes = [row[0] for row in axes]
    lines = []
    for axis, variable in zip(flat_axes, variables):
        line, = axis.plot([], [], linewidth=1.2)
        axis.set_ylabel(variable.requested)
        axis.grid(True, alpha=0.3)
        lines.append(line)
    flat_axes[-1].set_xlabel("time (s)")
    status = figure.text(0.01, 0.005, "connecting...", fontsize=9)
    figure.canvas.manager.set_window_title("dap monitor")
    figure.tight_layout(rect=(0, 0.03, 1, 1))

    def update(_: int):
        snapshot = list(rows)
        if snapshot:
            end = snapshot[-1]["elapsed_s"]
            begin = max(0.0, end - args.window)
            visible = [row for row in snapshot if row["elapsed_s"] >= begin]
            xs = [row["elapsed_s"] for row in visible]
            for axis, line, variable in zip(flat_axes, lines, variables):
                ys = [row["values"][variable.requested] for row in visible]
                line.set_data(xs, ys)
                axis.set_xlim(begin, max(args.window, end))
                if ys:
                    low, high = min(ys), max(ys)
                    pad = max((high - low) * 0.1, 1e-9)
                    axis.set_ylim(low - pad, high + pad)
            last = snapshot[-1]
            status.set_text(
                f"sample {last['seq']} | {rate:g} Hz | read {last['read_ms']:.2f} ms | "
                f"miss {last['deadline_misses']}"
            )
        if failure:
            status.set_text("ERROR: " + failure[-1])
        if stop.is_set() and args.duration is not None:
            plt.close(figure)
        return [*lines, status]

    animation = FuncAnimation(figure, update, interval=1000.0 / args.fps,
                              blit=False, cache_frame_data=False)
    _ = animation
    try:
        plt.show()
    finally:
        stop.set()
        worker.join(timeout=2.0)
    if failure:
        raise DapError(failure[-1])
    return 0


def cmd_monitor(args: argparse.Namespace, settings: Settings) -> int:
    names = _split_names(args.names)
    _, variables = _variables(settings, names)
    rate = _rate_for(args, settings, len(variables))
    if args.json or args.headless:
        return _headless_monitor(args, settings, variables, rate)
    return _gui_monitor(args, settings, variables, rate)


def cmd_record(args: argparse.Namespace, settings: Settings) -> int:
    names = _split_names(args.names)
    _, variables = _variables(settings, names)
    rate = _rate_for(args, settings, len(variables))
    destination = Path(args.out).resolve()
    destination.parent.mkdir(parents=True, exist_ok=True)
    count = 0
    misses = 0
    started = time.perf_counter()
    with destination.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=["seq", "elapsed_s", "read_ms", *names])
        writer.writeheader()
        with TclClient(settings.host, settings.port) as client:
            state = client.command("targets")
            if not _target_running(state):
                raise DapError("目标当前不是 running；dap 不会改变目标运行状态")
            for row in _sample_rows(client, variables, rate, args.duration):
                writer.writerow({
                    "seq": row["seq"], "elapsed_s": row["elapsed_s"],
                    "read_ms": row["read_ms"], **row["values"],
                })
                count += 1
                misses = row["deadline_misses"]
    report = {
        "ok": True,
        "path": str(destination),
        "samples": count,
        "duration_s": round(time.perf_counter() - started, 3),
        "requested_hz": rate,
        "deadline_misses": misses,
        "elf_sha256": hashlib.sha256(settings.elf.read_bytes()).hexdigest(),
    }
    print(_json_dump(report) if args.json else "\n".join(f"{k}: {v}" for k, v in report.items()))
    return 0


def _read_csv(path: Path) -> tuple[list[str], list[dict[str, float]]]:
    with path.open("r", newline="", encoding="utf-8-sig") as stream:
        reader = csv.DictReader(stream)
        fields = reader.fieldnames or []
        rows: list[dict[str, float]] = []
        for source in reader:
            row: dict[str, float] = {}
            for key, value in source.items():
                if key and value not in (None, ""):
                    try:
                        row[key] = float(value)
                    except ValueError:
                        pass
            rows.append(row)
    return fields, rows


def cmd_plot(args: argparse.Namespace, _: Settings) -> int:
    _configure_tk_runtime()
    try:
        import matplotlib.pyplot as plt
    except ImportError as exc:
        raise DapError("绘图需要 matplotlib") from exc
    path = Path(args.csv).resolve()
    fields, rows = _read_csv(path)
    x_name = "elapsed_s" if "elapsed_s" in fields else fields[0]
    names = _split_names(args.names) if args.names else [
        name for name in fields if name not in {"seq", "elapsed_s", "read_ms"}
    ]
    if not names:
        raise DapError("CSV 中没有可绘制的数据列")
    figure, axes = plt.subplots(len(names), 1, sharex=True, squeeze=False,
                               figsize=(10, max(3.5, 2.2 * len(names))))
    for axis, name in zip((row[0] for row in axes), names):
        points = [(row[x_name], row[name]) for row in rows if x_name in row and name in row]
        axis.plot([p[0] for p in points], [p[1] for p in points], linewidth=1.0)
        axis.set_ylabel(name)
        axis.grid(True, alpha=0.3)
    axes[-1][0].set_xlabel(x_name)
    figure.tight_layout()
    if args.out:
        figure.savefig(Path(args.out).resolve(), dpi=150)
    else:
        plt.show()
    return 0


def _pick(fields: list[str], explicit: str | None, candidates: list[str]) -> str | None:
    if explicit:
        return explicit if explicit in fields else None
    lowered = {field.lower(): field for field in fields}
    for candidate in candidates:
        if candidate in lowered:
            return lowered[candidate]
    return None


def cmd_pid_analyze(args: argparse.Namespace, _: Settings) -> int:
    path = Path(args.csv).resolve()
    fields, rows = _read_csv(path)
    time_name = _pick(fields, args.time_column, ["elapsed_s", "time_s", "time_ms"])
    signal_name = _pick(fields, args.signal_column,
                        ["angle", "angle_x", "balance.angle", "error", "pid_error"])
    output_name = _pick(fields, args.output_column,
                        ["output", "balance_output", "motor.output"])
    if signal_name is None:
        raise DapError("找不到 PID 信号列，请用 --signal-column 明确指定")
    values = [row[signal_name] - args.setpoint for row in rows if signal_name in row]
    if not values:
        raise DapError(f"{signal_name} 没有数值")
    duration = 0.0
    if time_name:
        times = [row[time_name] for row in rows if time_name in row]
        if time_name.endswith("_ms"):
            times = [value / 1000.0 for value in times]
        if len(times) > 1:
            duration = max(times) - min(times)
    crossings = sum(1 for a, b in zip(values, values[1:]) if (a < 0 <= b) or (a >= 0 > b))
    report: dict[str, Any] = {
        "ok": True,
        "file": str(path),
        "signal": signal_name,
        "samples": len(values),
        "duration_s": round(duration, 6),
        "sample_hz": round((len(values) - 1) / duration, 3) if duration > 0 else None,
        "mean_error": statistics.fmean(values),
        "mean_abs_error": statistics.fmean(abs(value) for value in values),
        "rms_error": math.sqrt(statistics.fmean(value * value for value in values)),
        "peak_abs_error": max(abs(value) for value in values),
        "stddev_error": statistics.pstdev(values),
        "zero_crossing_hz": crossings / (2.0 * duration) if duration > 0 else None,
    }
    if output_name:
        outputs = [row[output_name] for row in rows if output_name in row]
        report.update(output=output_name, output_peak=max(abs(value) for value in outputs))
        if args.output_limit is not None:
            report["output_saturation_ratio"] = (
                sum(abs(value) >= args.output_limit for value in outputs) / len(outputs)
            )
    print(_json_dump(report) if args.json else "\n".join(f"{k}: {v}" for k, v in report.items()))
    return 0


def cmd_server(_: argparse.Namespace, settings: Settings) -> int:
    if not settings.openocd_executable or not settings.openocd_executable.is_file():
        raise DapError("dap.json 中的 openocd.executable 无效")
    command = [str(settings.openocd_executable)]
    if settings.openocd_scripts:
        command.extend(["-s", str(settings.openocd_scripts)])
    command.extend(["-f", str(settings.openocd_config)])
    return subprocess.run(command, cwd=TOOL_ROOT, check=False).returncode


def _spawn_detached_server(settings: Settings) -> None:
    """flash 结束后在后台拉起独立 OpenOCD，恢复读侧服务。"""
    if not settings.openocd_executable:
        raise DapError("dap.json 中的 openocd.executable 无效，无法恢复 server")
    command = [str(settings.openocd_executable)]
    if settings.openocd_scripts:
        command.extend(["-s", str(settings.openocd_scripts)])
    command.extend(["-f", str(settings.openocd_config)])
    kwargs: dict[str, Any] = {}
    if os.name == "nt":
        kwargs["creationflags"] = getattr(subprocess, "DETACHED_PROCESS", 0) | getattr(subprocess, "CREATE_NEW_PROCESS_GROUP", 0)
    else:
        kwargs["start_new_session"] = True
    subprocess.Popen(command, cwd=TOOL_ROOT, stdout=subprocess.DEVNULL,
                     stderr=subprocess.DEVNULL, **kwargs)


def cmd_flash(args: argparse.Namespace, settings: Settings) -> int:
    """一条命令烧录：estop -> 关 server -> program verify + reset run -> 恢复 server -> 活性自检。
    烧录期间电机 PWM 冻结在最后指令值，所以 estop 必须先做。"""
    elf = Path(args.elf).resolve() if args.elf else settings.elf
    if not elf.is_file():
        raise DapError(f"ELF 不存在: {elf}")
    if not settings.openocd_executable or not settings.openocd_executable.is_file():
        raise DapError("dap.json 中的 openocd.executable 无效")

    estop_report: dict[str, Any] = {"attempted": False}
    try:
        with TclClient(settings.host, settings.port, timeout=3.0) as client:
            variable = index = ElfVariables(settings.elf, settings.aliases).resolve(
                settings.estop_symbol or "balance.enabled")
            client.command(f"mwb 0x{variable.address:x} 0")
            estop_report = {"attempted": True, "symbol": settings.estop_symbol}
        time.sleep(0.2)
    except (OSError, DapError):
        pass  # server 不在也允许烧录；PWM 冻结风险由用户自行评估

    # 烧录实例需要独占探针：优雅关闭仍在运行的 server
    try:
        with TclClient(settings.host, settings.port, timeout=3.0) as client:
            client.command("shutdown")
            time.sleep(0.5)
        estop_report["server_shutdown"] = True
    except (OSError, DapError):
        estop_report["server_shutdown"] = False

    command = [str(settings.openocd_executable)]
    if settings.openocd_scripts:
        command.extend(["-s", str(settings.openocd_scripts)])
    command.extend(["-f", str(settings.openocd_config)])
    command.extend([
        "-c", f"program {elf.as_posix()} verify",
        "-c", "reset run",
        "-c", "shutdown",
    ])
    started = time.perf_counter()
    result = subprocess.run(command, cwd=TOOL_ROOT, capture_output=True, text=True,
                            timeout=120, check=False)
    output = (result.stdout or "") + (result.stderr or "")
    verified = "Verified OK" in output
    report: dict[str, Any] = {
        "ok": bool(verified and result.returncode == 0),
        "elf": str(elf),
        "elf_sha256": hashlib.sha256(elf.read_bytes()).hexdigest(),
        "verified": verified,
        "estop": estop_report,
        "duration_s": round(time.perf_counter() - started, 3),
    }
    if not verified:
        report["openocd_tail"] = output.strip().splitlines()[-5:]

    # 恢复读侧服务并做活性自检
    if args.no_restart:
        report["server_restarted"] = False
    else:
        _spawn_detached_server(settings)
        alive = False
        for _ in range(20):
            time.sleep(0.5)
            try:
                with TclClient(settings.host, settings.port, timeout=2.0) as client:
                    state = client.command("targets")
                    alive = _target_running(state)
                    if alive:
                        break
            except (OSError, DapError):
                continue
        report["server_restarted"] = True
        report["target_running"] = alive
        report["ok"] = bool(report["ok"] and alive)

    if args.json:
        print(_json_dump(report))
    else:
        print("\n".join(f"{k}: {v}" for k, v in report.items()))
    return 0 if report["ok"] else 2


def _add_sampling_options(parser: argparse.ArgumentParser, duration_required: bool = False) -> None:
    parser.add_argument("names", nargs="+", help="ELF 变量/结构成员/别名，逗号或空格分隔")
    parser.add_argument("--rate", type=float, help="采样 Hz；默认按变量数量选择实测安全值")
    parser.add_argument("--duration", type=float, required=duration_required, help="采样秒数")
    parser.add_argument("--force", action="store_true", help="允许超过当前硬件实测保守频率")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="dap", description="运行中目标的读侧采集与白名单写参工具")
    parser.add_argument("--config", type=Path, default=DEFAULT_CONFIG)
    sub = parser.add_subparsers(dest="command", required=True)

    doctor = sub.add_parser("doctor", help="检查 OpenOCD、目标状态和 ELF")
    doctor.add_argument("--json", action="store_true")
    doctor.set_defaults(run=cmd_doctor)

    symbols = sub.add_parser("symbols", help="搜索 ELF 全局变量")
    symbols.add_argument("filter", nargs="?", default="")
    symbols.add_argument("--json", action="store_true")
    symbols.set_defaults(run=cmd_symbols)

    read = sub.add_parser("read", help="一次性读取运行中目标变量")
    read.add_argument("names", nargs="+")
    read.add_argument("--json", action="store_true")
    read.set_defaults(run=cmd_read)

    monitor = sub.add_parser("monitor", help="默认打开实时曲线；--json 输出 JSONL")
    _add_sampling_options(monitor)
    monitor.add_argument("--json", action="store_true", help="逐行输出 JSON，不打开图形")
    monitor.add_argument("--headless", action="store_true", help="无图形模式（输出 JSONL）")
    monitor.add_argument("--fps", type=float, default=30.0, help="图形刷新 FPS，默认 30")
    monitor.add_argument("--window", type=float, default=10.0, help="图形时间窗秒数")
    monitor.set_defaults(run=cmd_monitor)

    record = sub.add_parser("record", help="定时采样并保存 CSV")
    _add_sampling_options(record, duration_required=True)
    record.add_argument("--out", required=True)
    record.add_argument("--json", action="store_true", help="完成后输出 JSON 摘要")
    record.set_defaults(run=cmd_record)

    plot = sub.add_parser("plot", aliases=["draw"], help="绘制已记录 CSV")
    plot.add_argument("csv")
    plot.add_argument("names", nargs="*")
    plot.add_argument("--out", help="保存图片；省略则显示窗口")
    plot.set_defaults(run=cmd_plot)

    pid = sub.add_parser("pid", help="PID 数据处理")
    pid_sub = pid.add_subparsers(dest="pid_command", required=True)
    analyze = pid_sub.add_parser("analyze", help="输出 PID 记录统计")
    analyze.add_argument("csv")
    analyze.add_argument("--signal-column")
    analyze.add_argument("--output-column")
    analyze.add_argument("--time-column")
    analyze.add_argument("--setpoint", type=float, default=0.0)
    analyze.add_argument("--output-limit", type=float)
    analyze.add_argument("--json", action="store_true")
    analyze.set_defaults(run=cmd_pid_analyze)

    write = sub.add_parser("write", help="写入白名单变量（先写后读回验证，目标须 running）")
    write.add_argument("name")
    write.add_argument("value", type=str)
    write.add_argument("--json", action="store_true")
    write.set_defaults(run=cmd_write)

    estop = sub.add_parser("estop", help="急停：写 estop_symbol=0（不要求 running，不等于断电）")
    estop.add_argument("--json", action="store_true")
    estop.set_defaults(run=cmd_estop)

    flash = sub.add_parser("flash", help="烧录 dap.json 的 ELF：estop -> program verify -> reset run -> 恢复 server")
    flash.add_argument("--elf", help="覆盖默认 ELF 路径")
    flash.add_argument("--no-restart", action="store_true", help="烧录后不自动恢复 server")
    flash.add_argument("--json", action="store_true")
    flash.set_defaults(run=cmd_flash)

    server = sub.add_parser("server", help="前台启动本项目 OpenOCD（单实例）")
    server.set_defaults(run=cmd_server)
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        settings = Settings.load(args.config.resolve())
        return int(args.run(args, settings))
    except (DapError, OSError, socket.error) as exc:
        print(f"dap: {exc}", file=sys.stderr)
        return 2
    except KeyboardInterrupt:
        return 130


if __name__ == "__main__":
    raise SystemExit(main())
