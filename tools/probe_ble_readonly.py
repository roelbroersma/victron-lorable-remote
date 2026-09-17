"""Targeted Windows BLE inventory. No settings writes or load commands.

Only the requested address is retained. Reads are allow-listed. Optional
notification subscriptions write CCCDs, not application commands. Optional
pairing stores a Windows bond. --protocol-discovery sends only transport
negotiation and the VE.Smart GetDevices request. Run in research/ble-venv.
"""
import argparse
import asyncio
from datetime import datetime, timezone
import json
import os
import io
from pathlib import Path
import sys

sys.coinit_flags = 0  # headless Windows asyncio requires MTA
from bleak import BleakClient, BleakScanner

IDENTITY = {f"0000{n}-0000-1000-8000-00805f9b34fb" for n in
            ("2a00", "2a01", "2a24", "2a25", "2a26", "2a27", "2a28", "2a29")}
VENDOR_READ = {
    "97580002-ddf1-48be-b73e-182664615d8e",
    "97580006-ddf1-48be-b73e-182664615d8e",
    "306b0002-b081-4037-83dc-e59fcc3cdfd0",
    "306b0002-b081-4037-83dc-e59fcc3cdfd1",
}
NOTIFY = {f"306b000{n}-b081-4037-83dc-e59fcc3cdfd{ch}"
          for n in (2, 3, 4) for ch in (0, 1)}
CTRL = "306b0002-b081-4037-83dc-e59fcc3cdfd0"
LAST_DATA = "306b0003-b081-4037-83dc-e59fcc3cdfd0"

async def discover_protocol(client, report, query_device=False):
    # Wire meanings cross-checked against the original Victron protocol
    # research and patlux/ve-smart-telemetry. Never use SetValues (06),
    # SetPathValues (0c), PIN/PUK, or DFU writes here.
    sequence = [(CTRL, "fa80ff", "negotiate chunk size"),
                (CTRL, "f980", "receive credits"),
                (LAST_DATA, "01", "GetDevices")]
    report["protocol_requests"] = []
    for uuid, hex_data, purpose in sequence:
        await asyncio.wait_for(client.write_gatt_char(uuid, bytes.fromhex(hex_data), response=False), 6)
        report["application_writes"] += int(uuid == LAST_DATA)
        report["protocol_requests"].append({"uuid": uuid, "hex": hex_data, "purpose": purpose})
        print("PROTOCOL_REQUEST", purpose, hex_data, flush=True)
        await asyncio.sleep(0.3)
    if query_device:
        await asyncio.sleep(1)
        import cbor2
        # GetDevices on this BatteryProtect reports instance 0, not MPPT's 3.
        # Confirm it again in this connection before requesting its data.
        instances = set()
        for n in report["notifications"]:
            if n["uuid"] != LAST_DATA:
                continue
            raw = io.BytesIO(bytes.fromhex(n["hex"]))
            try:
                if cbor2.load(raw) == 2:
                    devices = cbor2.load(raw)
                    instances.update(devices[::2])
            except Exception:
                continue
        if 0 not in instances:
            raise RuntimeError("GetDevices did not confirm instance 0; stopping")
        queries = [(bytes.fromhex("0300"), "Subscribe instance 0")]
        for reg in (0x0100, 0x0102, 0x010a, 0x0200, 0x0201, 0xed8d,
                    0xeda8, 0xeda9, 0x0205, 0x0207, 0xe900):
            data = cbor2.dumps(5) + cbor2.dumps(0) + cbor2.dumps([reg])
            queries.append((data, f"GetValue 0x{reg:04x}"))
        for data, purpose in queries:
            await asyncio.wait_for(client.write_gatt_char(LAST_DATA, data, response=False), 6)
            report["application_writes"] += 1
            report["protocol_requests"].append({"uuid": LAST_DATA, "hex": data.hex(), "purpose": purpose})
            print("PROTOCOL_REQUEST", purpose, data.hex(), flush=True)
            await asyncio.sleep(0.4)

async def pair_with_supplied_pin(client, report):
    from winrt.windows.devices.enumeration import (
        DeviceInformation, DevicePairingKinds, DevicePairingProtectionLevel,
        DevicePairingResultStatus,
    )
    pin = os.environ.get("LORABLE_TEST_PIN", "")
    if len(pin) != 6 or not pin.isdecimal():
        raise ValueError("Set LORABLE_TEST_PIN to the user-supplied six-digit PIN")
    # Bleak 3.0.2 only offers ConfirmOnly. Use its already connected WinRT
    # requester with the native ProvidePin ceremony for this particular device.
    info = await DeviceInformation.create_from_id_async(
        client._backend._requester.device_information.id)
    report["paired_before"] = info.pairing.is_paired
    if info.pairing.is_paired:
        report["pair_result"] = "ALREADY_PAIRED"
        print("PAIR ALREADY_PAIRED", flush=True)
        return
    custom = info.pairing.custom
    def requested(sender, event):
        report["pairing_kind"] = event.pairing_kind.name
        print("PAIR_CEREMONY", event.pairing_kind.name, flush=True)
        if event.pairing_kind == DevicePairingKinds.PROVIDE_PIN:
            event.accept_with_pin(pin)
        elif event.pairing_kind == DevicePairingKinds.CONFIRM_ONLY:
            event.accept()
    token = custom.add_pairing_requested(requested)
    try:
        result = await asyncio.wait_for(custom.pair_with_protection_level_async(
            DevicePairingKinds.PROVIDE_PIN | DevicePairingKinds.CONFIRM_ONLY,
            DevicePairingProtectionLevel.ENCRYPTION_AND_AUTHENTICATION), 45)
        report["pair_result"] = result.status.name
        report["pair_protection"] = result.protection_level_used.name
        print("PAIR_RESULT", result.status.name, result.protection_level_used.name, flush=True)
        if result.status not in (DevicePairingResultStatus.PAIRED,
                                 DevicePairingResultStatus.ALREADY_PAIRED):
            raise RuntimeError("Pairing did not succeed: " + result.status.name)
    finally:
        custom.remove_pairing_requested(token)

async def probe(args, report):
    found = asyncio.Event()
    selected = None
    def seen(device, adv):
        nonlocal selected
        if device.address.upper() != args.mac.upper():
            return
        selected = device
        report["advertisement"] = {"address": device.address, "name": adv.local_name,
            "rssi_dbm": adv.rssi, "services": adv.service_uuids,
            "manufacturer_data": {str(k): bytes(v).hex() for k,v in adv.manufacturer_data.items()},
            "service_data": {k: bytes(v).hex() for k,v in adv.service_data.items()}}
        found.set()
    async with BleakScanner(detection_callback=seen):
        await asyncio.wait_for(found.wait(), 15)
        await asyncio.sleep(2)  # obtain scan-response name
    print("TARGET", json.dumps(report["advertisement"]), flush=True)
    if args.scan_only:
        return
    client = BleakClient(selected, timeout=25, winrt={"use_cached_services": False})
    try:
        print("Connecting; no settings writes or load commands...", flush=True)
        await asyncio.wait_for(client.connect(), 35)
        report["connected"] = client.is_connected
        report["mtu"] = client.mtu_size
        report["services"] = []
        print("CONNECTED", client.is_connected, "MTU", client.mtu_size, flush=True)
        if args.pair:
            await pair_with_supplied_pin(client, report)
        for service in client.services:
            item = {"uuid": service.uuid, "handle": service.handle,
                    "description": service.description, "characteristics": []}
            report["services"].append(item)
            print("SERVICE", service.uuid, service.handle, service.description, flush=True)
            for char in service.characteristics:
                c = {"uuid": char.uuid, "handle": char.handle, "properties": char.properties,
                     "description": char.description,
                     "descriptors": [{"uuid": d.uuid, "handle": d.handle} for d in char.descriptors]}
                item["characteristics"].append(c)
                print("CHAR", json.dumps(c), flush=True)
                if "read" in char.properties and (char.uuid in IDENTITY or
                        (args.vendor_reads and char.uuid in VENDOR_READ)):
                    try:
                        value = bytes(await asyncio.wait_for(client.read_gatt_char(char), 6))
                        c["read_hex"] = value.hex()
                        if char.uuid in IDENTITY:
                            c["identity_text"] = value.decode("utf-8", errors="replace")
                        print("READ", char.uuid, value.hex(), flush=True)
                    except Exception as e:
                        c["read_error"] = str(e)
                        print("READ_ERROR", char.uuid, str(e), flush=True)
        if args.listen:
            report["subscriptions"] = []
            report["notifications"] = []
            def notification(char, value):
                item = {"uuid": char.uuid, "hex": bytes(value).hex(),
                        "utc": datetime.now(timezone.utc).isoformat()}
                if len(report["notifications"]) < 512:
                    report["notifications"].append(item)
                    print("NOTIFICATION", json.dumps(item), flush=True)
            for service in client.services:
                for char in service.characteristics:
                    if "notify" not in char.properties or char.uuid not in NOTIFY:
                        continue
                    item = {"uuid": char.uuid}
                    report["subscriptions"].append(item)
                    try:
                        await asyncio.wait_for(client.start_notify(char, notification), 8)
                        item["enabled"] = True
                        print("SUBSCRIBED", char.uuid, flush=True)
                    except Exception as e:
                        item["error"] = type(e).__name__ + ": " + str(e)
                        print("SUBSCRIBE_ERROR", char.uuid, item["error"], flush=True)
            if args.protocol_discovery:
                required = {CTRL, LAST_DATA, "306b0004-b081-4037-83dc-e59fcc3cdfd0"}
                enabled = {s["uuid"] for s in report["subscriptions"] if s.get("enabled")}
                if not required <= enabled:
                    raise RuntimeError("Required notifications unavailable; no protocol requests sent")
                await discover_protocol(client, report, args.query_device)
            await asyncio.sleep(args.listen)
    finally:
        await asyncio.wait_for(client.disconnect(), 10)
        report["disconnected"] = not client.is_connected

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mac")
    parser.add_argument("--scan-only", action="store_true")
    parser.add_argument("--vendor-reads", action="store_true")
    parser.add_argument("--pair", action="store_true", help="Pair with LORABLE_TEST_PIN; never logs the PIN")
    parser.add_argument("--protocol-discovery", action="store_true", help="VE.Smart transport negotiation + GetDevices only; requires --listen")
    parser.add_argument("--query-device", action="store_true", help="Subscribe and query identity/status registers and path list on confirmed instance 0")
    parser.add_argument("--listen", type=int, default=0, choices=range(0, 31), metavar="0..30")
    args = parser.parse_args()
    if args.protocol_discovery and not args.listen:
        parser.error("--protocol-discovery requires --listen")
    if args.query_device and not args.protocol_discovery:
        parser.error("--query-device requires --protocol-discovery")
    now = datetime.now(timezone.utc)
    report = {"started_utc": now.isoformat(), "target": args.mac,
              "pair_requested": args.pair, "application_writes": 0,
              "configuration_writes": 0, "load_commands": 0}
    exit_code = 0
    try:
        asyncio.run(probe(args, report))
    except Exception as e:
        report["error"] = type(e).__name__ + ": " + str(e)
        print("ERROR", report["error"], flush=True)
        exit_code = 1
    finally:
        dest = Path(__file__).resolve().parent.parent / "artifacts_private" / ("ble-inventory-" + now.strftime("%Y%m%d-%H%M%S-%f") + ".json")
        with dest.open("x", encoding="utf-8") as f:
            json.dump(report, f, indent=2)
        print("REPORT", str(dest), flush=True)
    sys.exit(exit_code)
