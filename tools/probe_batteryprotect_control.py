"""Guarded, one-cycle test of the owner's unloaded Smart BatteryProtect.

Default: read-only. --switch-cycle: OFF then restore the original ON mode.
Requires an explicitly supplied MAC and expected serial; only tested PID A3B1
is accepted. Never put a user's MAC, serial or PIN into published source.
Never changes protection thresholds, BMS mode, PIN, firmware or pairing keys.
Wire mapping researched from the official VictronConnect 6.42 APK; see
BATTERYPROTECT-RESEARCH.md. Requires an existing Windows Bluetooth bond.
"""
import argparse
import asyncio
from datetime import datetime, timezone
import io
import json
from pathlib import Path
import re
import sys
import time

sys.coinit_flags = 0
from bleak import BleakClient, BleakScanner
import cbor2

CTRL = "306b0002-b081-4037-83dc-e59fcc3cdfd0"
LAST = "306b0003-b081-4037-83dc-e59fcc3cdfd0"
DATA = "306b0004-b081-4037-83dc-e59fcc3cdfd0"
MODE, STATE, OUTPUT, VIN, VOUT, OFF_REASON = 0x0200, 0x0201, 0xeda8, 0xed8d, 0xeda9, 0x0207
READABLE = {0x0100, 0x0102, 0x010a, MODE, STATE, OUTPUT, VIN, VOUT, OFF_REASON, 0xe900}

def parse_frame(payload):
    """Decode complete concatenated CBOR records; fail closed on unknowns."""
    if len(payload) > 65536:
        raise ValueError("Oversize frame")
    stream = io.BytesIO(payload)
    records = []
    while stream.tell() < len(payload):
        op = cbor2.load(stream)
        if op == 2:
            records.append((op, cbor2.load(stream)))
        elif op in (7, 8, 9):
            fields = [cbor2.load(stream) for _ in range(3)]
            if op == 8 and not isinstance(fields[2], bytes):
                raise ValueError("Malformed register value")
            records.append((op, *fields))
        else:
            raise ValueError(f"Unsupported response opcode {op!r}")
    return records

def request(*values):
    return b"".join(cbor2.dumps(v) for v in values)

class Session:
    def __init__(self, client, report):
        self.client, self.report = client, report
        self.buffer = bytearray()
        self.values, self.errors, self.acks = {}, {}, {}
        self.events = {}
        self.instances = None
        self.received_chunks = 0
        self.guard_ok = False

    def notification(self, char, data):
        raw = bytes(data)
        self.report["notifications"].append({"uuid": char.uuid, "hex": raw.hex(), "t": time.monotonic()})
        if char.uuid == CTRL:
            return
        self.received_chunks += 1
        self.buffer.extend(raw)
        if len(self.buffer) > 65536:
            self.buffer.clear()
            self.report["parse_errors"].append("reassembly overflow")
            return
        if char.uuid != LAST:
            return
        payload = bytes(self.buffer)
        self.buffer.clear()
        try:
            for row in parse_frame(payload):
                if row[0] == 2:
                    self.instances = row[1][::2]
                elif row[0] == 8 and row[1] == 0:
                    self.values[row[2]] = row[3]
                    if row[2] in self.events:
                        self.events[row[2]].set()
                elif row[0] == 9 and row[1] == 0:
                    self.acks[row[2]] = row[3]
                    if row[3] != 0:
                        self.errors[row[2]] = row[3]
                        if row[2] in self.events:
                            self.events[row[2]].set()
        except Exception as e:
            self.report["parse_errors"].append(str(e))

    async def send(self, uuid, data, purpose):
        if self.received_chunks >= 32:
            await self.client.write_gatt_char(CTRL, bytes.fromhex("f941"), response=False)
            self.report["writes"].append({"uuid": CTRL, "hex": "f941", "purpose": "receive credits"})
            self.received_chunks = 0
        # Record an attempted write before awaiting, so errors cannot hide it.
        row = {"uuid": uuid, "hex": data.hex(), "purpose": purpose, "t": time.monotonic()}
        self.report["writes"].append(row)
        await asyncio.wait_for(self.client.write_gatt_char(uuid, data, response=False), 6)
        row["submitted"] = True

    async def open(self):
        for uuid in (CTRL, LAST, DATA):
            await asyncio.wait_for(self.client.start_notify(uuid, self.notification), 8)
        # A fresh read also confirms access to the SMP-protected control channel.
        control = await asyncio.wait_for(self.client.read_gatt_char(CTRL), 6)
        self.report["control_info"] = bytes(control).hex()
        for data in (bytes.fromhex("fa80ff"), bytes.fromhex("f980")):
            await self.send(CTRL, data, "transport negotiation")
            await asyncio.sleep(0.2)
        await self.send(LAST, request(1), "GetDevices")
        for _ in range(30):
            if self.instances is not None:
                break
            await asyncio.sleep(0.1)
        if self.instances != [0]:
            raise RuntimeError(f"Unexpected device instances: {self.instances}")
        # Live BatteryProtect test: GETs before Subscribe receive credits but
        # no value response. Subscription is session-only, not a setting.
        await self.send(LAST, request(3, 0), "Subscribe instance 0")
        await asyncio.sleep(0.3)
        product = await self.read(0x0100)
        serial = await self.read(0x010a)
        if product != bytes.fromhex("00b1a3fe") or serial != self.report["expected_serial"].encode("ascii"):
            raise RuntimeError("Target identity guard failed; no load commands allowed")
        self.guard_ok = True

    async def read(self, reg):
        if reg not in READABLE:
            raise ValueError("Register not in read allowlist")
        event = self.events[reg] = asyncio.Event()
        self.errors.pop(reg, None)
        await self.send(LAST, request(5, 0, [reg]), f"GetValue {reg:04x}")
        await asyncio.wait_for(event.wait(), 5)
        if reg in self.errors:
            raise RuntimeError(f"Register {reg:04x} error {self.errors[reg]}")
        return self.values[reg]

    async def snapshot(self, label):
        result = {"label": label, "utc": datetime.now(timezone.utc).isoformat()}
        for name, reg in (("mode", MODE), ("state", STATE), ("output_state", OUTPUT),
                          ("input_centivolts", VIN), ("output_centivolts", VOUT), ("off_reason", OFF_REASON)):
            raw = await self.read(reg)
            result[name] = int.from_bytes(raw, "little", signed=(reg == VIN))
        self.report["snapshots"].append(result)
        print("SNAPSHOT", json.dumps(result), flush=True)
        return result

    async def set_mode(self, mode):
        if not self.guard_ok or self.client.address.upper() != self.report["target"] or mode not in (3, 4):
            raise RuntimeError("Mode write guard failed")
        self.acks.pop(MODE, None)
        await self.send(LAST, request(6, 0, [MODE, bytes([mode])]), "Set BatteryProtect mode " + str(mode))
        await asyncio.sleep(0.3)
        actual = await self.read(MODE)
        if actual != bytes([mode]):
            raise RuntimeError("Mode readback did not match")
        print("MODE_VERIFIED", mode, "ACK", self.acks.get(MODE), flush=True)

    async def wait_output(self, value, timeout=35):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if int.from_bytes(await self.read(OUTPUT), "little") == value:
                return
            await asyncio.sleep(1)
        raise TimeoutError(f"Actual output did not become {value}; protections were not overridden")

async def run(args, report):
    device = await BleakScanner.find_device_by_address(args.mac, timeout=15)
    if device is None:
        raise RuntimeError("Target not in range")
    client = BleakClient(device, timeout=25, winrt={"use_cached_services": False})
    restore_needed = False
    original_mode = None
    session = None
    try:
        await asyncio.wait_for(client.connect(), 35)
        session = Session(client, report)
        await session.open()
        before = await session.snapshot("before")
        original_mode = before["mode"]
        if not args.switch_cycle:
            return
        if original_mode != 3 or before["output_state"] != 1 or before["off_reason"] != 0:
            raise RuntimeError("Cycle requires original mode ON, output ON, and no off reason")
        report["cycle_started"] = True
        restore_needed = True
        await session.set_mode(4)
        await session.wait_output(0, timeout=10)
        await session.snapshot("off_verified")
    finally:
        if restore_needed and session is not None:
            try:
                # Freshly revalidate identity if a reconnect is needed. Never
                # silently abandon restoration after an uncertain write result.
                if not client.is_connected:
                    await asyncio.wait_for(client.connect(), 35)
                    session = Session(client, report)
                    await session.open()
                await session.set_mode(original_mode)
                report["original_mode_restored"] = True
                await session.wait_output(1)
                await session.snapshot("restored_on_verified")
                report["output_restored"] = True
            except Exception as e:
                report["restoration_error"] = type(e).__name__ + ": " + str(e)
                print("RESTORATION_ERROR", report["restoration_error"], flush=True)
        await asyncio.wait_for(client.disconnect(), 10)
        report["disconnected"] = not client.is_connected

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mac", required=True, help="Target address, colon-separated")
    parser.add_argument("--serial", required=True, help="Expected serial, checked before any mode write")
    parser.add_argument("--switch-cycle", action="store_true")
    args = parser.parse_args()
    args.mac = args.mac.upper()
    if not re.fullmatch(r"(?:[0-9A-F]{2}:){5}[0-9A-F]{2}", args.mac):
        parser.error("Invalid Bluetooth address")
    if not re.fullmatch(r"[A-Za-z0-9]{6,32}", args.serial):
        parser.error("Invalid expected serial")
    now = datetime.now(timezone.utc)
    report = {"started_utc": now.isoformat(), "target": args.mac, "expected_serial": args.serial,
              "switch_cycle_requested": args.switch_cycle, "writes": [],
              "notifications": [], "parse_errors": [], "snapshots": []}
    exit_code = 0
    try:
        asyncio.run(run(args, report))
    except Exception as e:
        report["error"] = type(e).__name__ + ": " + str(e)
        print("ERROR", report["error"], flush=True)
        exit_code = 1
    if report.get("restoration_error"):
        exit_code = 2
    dest = Path(__file__).resolve().parent.parent / "artifacts_private" / ("batteryprotect-control-" + now.strftime("%Y%m%d-%H%M%S-%f") + ".json")
    with dest.open("x", encoding="utf-8") as file:
        json.dump(report, file, indent=2)
    print("REPORT", str(dest), flush=True)
    sys.exit(exit_code)
