"""Safely reproduce selected reference-client behaviors without network access.

Only top-level function AST nodes are compiled from the downloaded reference
source. The harness supplies fake sockets and synthetic responses; it never
imports or executes either complete authentication script.
"""

from __future__ import annotations

import ast
import builtins
import json
import re
import struct
from pathlib import Path
from types import SimpleNamespace


ROOT = Path(__file__).resolve().parents[1]
PY_SOURCE = ROOT / "reference/jlu-drcom-client/jlu-drcom-py3/newclinet-py3.py"


def isolated_function(path: Path, name: str, namespace: dict) -> tuple[object, ast.FunctionDef]:
    source = path.read_text(encoding="utf-8")
    tree = ast.parse(source, filename=str(path))
    node = next(
        item for item in tree.body
        if isinstance(item, (ast.FunctionDef, ast.AsyncFunctionDef)) and item.name == name
    )
    module = ast.Module(body=[node], type_ignores=[])
    exec(compile(ast.fix_missing_locations(module), str(path), "exec"), namespace)
    return namespace[name], node


def line_range(node: ast.AST) -> list[int]:
    return [int(node.lineno), int(node.end_lineno)]


def finding(
    key: str,
    title: str,
    node: ast.AST,
    observed: str,
    impact: str,
    limitation: str,
) -> dict:
    return {
        "id": key,
        "title": title,
        "source": str(PY_SOURCE.relative_to(ROOT)).replace("\\", "/"),
        "lines": line_range(node),
        "method": "Parsed the source, compiled only this function AST node, and supplied local synthetic inputs.",
        "observed": observed,
        "impact": impact,
        "limitation": limitation,
    }


def run() -> dict:
    results: list[dict] = []

    # bind_nic: fake ioctl reports a plausible interface address. The socket
    # constructor below is a local stub, so no OS socket is created.
    class FakeSocket:
        def fileno(self) -> int:
            return 73

    class FakeSocketModule:
        AF_INET = 2
        SOCK_DGRAM = 2

        @staticmethod
        def socket(*args: object) -> FakeSocket:
            return FakeSocket()

        @staticmethod
        def inet_ntoa(data: bytes) -> str:
            assert data == bytes((192, 0, 2, 44))
            return "192.0.2.44"

    class FakeFcntl:
        calls = 0

        @classmethod
        def ioctl(cls, fd: int, operation: int, request: bytes) -> bytes:
            cls.calls += 1
            assert fd == 73 and operation == 0x8915
            assert len(request) == 256
            return b"\x00" * 20 + bytes((192, 0, 2, 44))

    def fake_import(name: str, globals=None, locals=None, fromlist=(), level=0):
        if name == "fcntl":
            return FakeFcntl
        return builtins.__import__(name, globals, locals, fromlist, level)

    fake_builtins = dict(vars(builtins))
    fake_builtins["__import__"] = fake_import
    bind_ns = {"__builtins__": fake_builtins, "socket": FakeSocketModule,
               "struct": struct, "nic_name": b"Ethernet"}
    bind_nic, bind_node = isolated_function(PY_SOURCE, "bind_nic", bind_ns)
    bind_result = bind_nic()
    assert bind_result == "0.0.0.0" and FakeFcntl.calls == 1
    results.append(finding(
        "bind_nic-finally-return",
        "bind_nic discards a successfully resolved interface address",
        bind_node,
        f"mock ioctl/inet_ntoa resolved 192.0.2.44; bind_nic() returned {bind_result!r}; fake ioctl calls={FakeFcntl.calls}",
        "A configured interface bind silently falls back to 0.0.0.0, so the client does not bind to the selected NIC address.",
        "The test uses a fake socket object and synthetic ioctl bytes. It does not test routing or actual adapter behavior.",
    ))

    # checksum: include a newline inside the first four-byte group and one
    # trailing byte. bytes-regex dot treats newline specially and findall
    # drops incomplete groups.
    checksum_ns = {"re": re, "struct": struct}
    checksum, checksum_node = isolated_function(PY_SOURCE, "checksum", checksum_ns)
    payload = b"\x01\x02\x0a\x04\x05\x06\x07\x08\x09"
    chunks = re.findall(b"....", payload)
    checksum_result = checksum(payload)
    assert chunks == [b"\x04\x05\x06\x07"]
    assert checksum_result == bytes.fromhex("201d2efe")
    results.append(finding(
        "checksum-newline-and-tail",
        "checksum skips a group containing LF and ignores a short tail",
        checksum_node,
        f"payload={payload.hex()}; regex chunks={[chunk.hex() for chunk in chunks]}; checksum={checksum_result.hex()}",
        "For packet bytes containing 0x0A, the checksum loop can omit bytes and then regroup later bytes; a final 1–3 byte tail is also ignored.",
        "The experiment establishes Python regex behavior for synthetic bytes. It does not establish whether a particular campus packet includes LF or what the server expects for that packet.",
    ))

    # ror: a synthetic 17-byte password exceeds the 16-byte MD5 digest.
    ror_ns: dict = {}
    ror, ror_node = isolated_function(PY_SOURCE, "ror", ror_ns)
    short_output = ror(bytes(range(16)), bytes(range(16)))
    try:
        ror(bytes(range(16)), bytes(range(17)))
    except Exception as exc:  # report the exact deterministic exception
        long_error = f"{type(exc).__name__}: {exc}"
    else:  # pragma: no cover - makes unexpected behavior fail visibly
        raise AssertionError("17-byte password unexpectedly succeeded")
    assert len(short_output) == 16
    assert long_error.startswith("IndexError:")
    results.append(finding(
        "ror-password-over-md5",
        "ror indexes beyond the MD5 digest for passwords longer than 16 bytes",
        ror_node,
        f"16-byte password output length={len(short_output)}; 17-byte password raised {long_error}",
        "Packet construction can fail with IndexError for a password longer than the 16-byte MD5 digest, before the client can send a login packet.",
        "Only the helper was run with fixed synthetic bytes. No credentials or login code were used.",
    ))

    # login: fake the complete request/response boundary. The one-byte response
    # starts with 0x04, so the code accepts it and returns an empty tail slice.
    class FakeLoginSocket:
        def __init__(self) -> None:
            self.sent: list[tuple[bytes, tuple[str, int]]] = []
            self.recv_calls = 0

        def sendto(self, packet: bytes, address: tuple[str, int]) -> None:
            self.sent.append((packet, address))

        def recvfrom(self, size: int) -> tuple[bytes, tuple[str, int]]:
            self.recv_calls += 1
            assert size == 1024
            return b"\x04", ("203.0.113.7", 61440)

    fake_login_socket = FakeLoginSocket()
    login_ns = {
        "SALT": b"",
        "UNLIMITED_RETRY": False,
        "challenge": lambda server, timestamp: b"\x01\x02\x03\x04",
        "time": SimpleNamespace(time=lambda: 1234.0),
        "random": SimpleNamespace(randint=lambda lo, hi: lo),
        "mkpkt": lambda salt, user, password, mac: b"synthetic-login-packet",
        "s": fake_login_socket,
        "mac": 0,
        "log": lambda *args, **kwargs: None,
        "exit": lambda code=0: (_ for _ in ()).throw(AssertionError(f"unexpected exit {code}")),
    }
    login, login_node = isolated_function(PY_SOURCE, "login", login_ns)
    login_result = login(b"synthetic-user", b"synthetic-password", "203.0.113.7")
    assert login_result == b""
    assert len(fake_login_socket.sent) == 1 and fake_login_socket.recv_calls == 1
    results.append(finding(
        "login-short-04-response",
        "login accepts a one-byte 0x04 response and returns an empty tail",
        login_node,
        f"fake response=04; fake send calls={len(fake_login_socket.sent)}; recv calls={fake_login_socket.recv_calls}; returned tail={login_result.hex()}",
        "A truncated or malformed datagram beginning with 0x04 is treated as successful login; the caller receives no keepalive tail bytes.",
        "The test uses a fake socket and deterministic stubs. It does not model the real server response or prove that the server emits such a short datagram.",
    ))

    # Static control-flow observation; deliberately not run because the
    # reference function contains unbounded retry loops.
    keepalive_path = ROOT / "reference/jlu-drcom-client/jlu-drcom-py3/newclinet-py3.py"
    # Read the file here so a missing reference checkout fails loudly while
    # keeping the evidence locations below explicit and reviewable.
    keepalive_path.read_text(encoding="utf-8")
    results.append({
        "id": "keepalive-errors-do-not-reenter-login",
        "title": "keep_alive2 catches errors and continues its keepalive loop",
        "source": "reference/jlu-drcom-client/jlu-drcom-py3/newclinet-py3.py",
        "lines": {"keep_alive2": [218, 243], "main_call_sequence": [379, 392]},
        "method": "Static control-flow inspection only; function not executed because its retry loops are unbounded.",
        "observed": "The keep_alive2 loop catches every exception and continues. main calls login before keep_alive1 and keep_alive2; there is no re-login call in the keepalive exception handler.",
        "impact": "A timeout or transport error during keepalive does not trigger the script's login flow; it can keep retrying keepalive against a stale session.",
        "limitation": "Static source evidence only. It does not identify the cause of the reported ~101-second disconnect or prove server-side timeout behavior.",
    })

    cpp_review = [
        {
            "id": "win32-keepalive-buffer-length-mismatch",
            "source": "reference/dr-jlu-win32/Login/dogcom/keepalive.cpp",
            "lines": {"declaration_and_short_send": [64, 68], "oversized_send": [98, 105]},
            "method": "Static C++ review; no authentication code executed.",
            "observed": "keepalive_1_packet2 is declared as 38 bytes, then sendto reads 42 bytes; the 8-byte keepalive_1_packet1 is also passed to print_packet with length 42.",
            "impact": "The second send reads four bytes past the packet buffer, and the debug print reads 34 bytes past the 8-byte array. This is undefined behavior and can disclose adjacent stack bytes or corrupt protocol output.",
            "limitation": "Source-level size analysis only; exact runtime effects depend on compiler and stack layout.",
        },
        {
            "id": "win32-config-copy-can-overflow",
            "source": "reference/dr-jlu-win32/Login/dogcom/configparse.cpp",
            "lines": [7, 12],
            "related_sources": {
                "reference/dr-jlu-win32/Login/dogcom/configparse.h": [4, 7],
                "reference/dr-jlu-win32/Login/dogcom/login.h": [12, 15],
                "reference/dr-jlu-win32/Login/WinMain.cpp": [156, 157],
            },
            "method": "Static C++ review; no credentials used.",
            "observed": "fillConfig copies username/password with strcpy into username[36]/password[20]. The GUI staging buffers are 48 bytes and GetWindowTextA is called with their full sizes.",
            "impact": "A username of 36+ bytes or password of 20+ bytes can overflow the destination struct fields when passed to fillConfig.",
            "limitation": "Static size/control-flow comparison only; no overflow was executed.",
        },
    ]

    return {
        "purpose": "Offline-only, synthetic-data audit of downloaded JLU Dr.COM reference clients.",
        "reference_revisions": {
            "jlu-drcom-client": "2ba09ce24041c4ab7021ddbc07a366e9ae3e1c5d",
            "dr-jlu-win32": "6568d479300575215adee5813801726aa47f19d3",
        },
        "safety": {
            "executed_authentication_entrypoint": False,
            "created_real_network_socket": False,
            "used_real_credentials": False,
            "executed_unbounded_keepalive_loop": False,
            "compiled_legacy_cpp": False,
        },
        "reproductions_and_reviews": results,
        "additional_static_reviews": cpp_review,
        "limitations": [
            "These checks verify local source behavior under synthetic inputs; they do not explain the user's exact disconnect timing.",
            "No packet was sent to a campus server, and no live route, proxy, TUN, or client state was inspected or changed.",
        ],
    }


if __name__ == "__main__":
    report = run()
    output_path = ROOT / "docs/offline-audit-results.json"
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    for item in report["reproductions_and_reviews"]:
        print(f"{item['id']}: {item['observed']}")
    print(f"report: {output_path}")
