# Victron LoRaBLE Remote

Switch on a modem, router or other load remotely — without leaving that equipment powered all the time.

[Nederlands](README.md) · [Download](https://github.com/roelbroersma/victron-lorable-remote/releases/latest) · [Installation](docs/INSTALL.md) · [LoRaWAN setup](docs/NETWORKS.md) · [Examples](docs/EXAMPLES.md)

LoRaBLE Remote bridges **LoRaWAN to Bluetooth and relays**. A small RAK WisBlock board receives the initial command and operates a Victron Smart MPPT, Smart BatteryProtect or relay. Power your 4G/5G modem or WiFi router only when you need it.

Use your own LoRaWAN network — for example, a **Milesight UG63 / UG65** — or a network such as **The Things Network (TTN)**. Four network profiles provide priority, fallback and a configurable return timer. One network session is active at a time.

## Get started

1. Download **LoRaBLE-Remote-4.11.0-Windows.zip** from the [latest release](https://github.com/roelbroersma/victron-lorable-remote/releases/latest) and extract the entire ZIP.
2. Connect the board's normal USB data port and open **Install.cmd**. Choose NL or EN and follow the prompts. Read the [installation requirements and compatibility](docs/INSTALL.md).
3. Connect to WiFi **Victron LoRaBLE Remote**, password **CHANGE-ME-FIRST**, and open **http://192.168.4.1/**. Change the password immediately.
4. Select your Bluetooth device, add functions and enter your LoRaWAN network credentials. Assign functions to input edges or LoRaWAN commands.

Initial factory installation uses USB and temporarily connects the PC to device WiFi. The wizard handles that connection; no Arduino IDE, Python or separate programming pins are needed for the supported factory route. Subsequent updates use **one complete `.bin` file** through USB or **Manage → Firmware update**.

## Features

| Component | Capabilities |
|---|---|
| Victron Smart MPPT | Eight LOAD output operating modes |
| Victron Smart BatteryProtect | ON/OFF with mode and output-state readback; product A3B1, 12/24V-100A |
| Generic Bluetooth | Custom GATT service, characteristic and 1–20 command bytes |
| Bluetooth functions | Up to ten named commands for one target device |
| Inputs and relays | Rising/falling edges trigger a Bluetooth function, LoRa status message and/or relay action |
| LoRaWAN | Four OTAA profiles, priority, fallback, preemption, Class A/C and command permissions |
| WiFi | Configurable windows after startup, input events or LoRa commands |
| Web interface | English/Dutch, field help, uptime, recent events, manual controls and an energy estimate |
| Management | Persistent settings, export/import and complete firmware updates |

![Web interface — status](docs/images/status.png)

Interface shown with example settings.

## Hardware

| Component | Purpose |
|---|---|
| **RAK11162** with RAK11160 module | LoRaWAN, WiFi and Bluetooth |
| **RAK19010** | WisBlock baseboard |
| **RAK19012** | USB/LiPo/solar power module with USB programming connection |
| **RAK19016** | Alternative 5–24V power module for operation after installation |
| **RAK13001**, optional | One isolated 12–24V DC input and one relay output |
| **RAK13007**, optional | One relay output, no input |
| Antennas | Appropriate LoRa and 2.4GHz antennas |

LoRaWAN → Bluetooth works without an I/O module. Select the fitted I/O board in settings; modules are not automatically detected. Use one power module per power slot.

RAK13001 detects **12–24V DC presence**: rising means voltage appears; falling means it disappears. It is not a voltmeter. Never connect 12V directly to a processor pin. Check module routing: input **WB_IO3**, relay **WB_IO4**. Both relays are non-latching and consume coil power while energized. Relay contacts do not supply power themselves.

## LoRaWAN commands

Send **hex bytes**, not ASCII text, on the configured FPort, default **10**. Enable the corresponding function and downlink permission first.

| Payload | Action |
|---|---|
| `01` … `09`, `0A` | Bluetooth function 1 … 10 |
| `10` | Relay OFF |
| `11` | Relay ON |
| `12` | Relay pulse for the configured duration |
| `20` | Request status |

Function 10 is **`0A`**, not `10`. The [payload codec](stm32/lorawan-payload-codec.js) includes Milesight `Decode` and TTN `decodeUplink`. See [network setup](docs/NETWORKS.md) for registration, channel plans, Class C and fallback.

## Energy and operation

The benefit comes from equipment you can **leave switched off**. Class C keeps the LoRa receiver available almost continuously; it is not a microamp sleep mode. Class A receives only after an uplink. The portal shows a datasheet-based daily estimate in mAh or Wh, including WiFi and relay scenarios. Conversion losses, connected loads and actual radio traffic determine total consumption.

WiFi pauses during a Bluetooth command and returns within the configured window. Retries occur only after failure. MPPT User defined and AES modes use thresholds and times already configured in VictronConnect. Other BatteryProtect product variants are refused; BMS mode and protection thresholds remain unchanged.

Power the board independently of its switched load. Keep the gateway reachable while the modem is off. Use appropriate wiring, fuses and a local disconnect. This project is not a safety controller; command reception and successful execution are separate statuses.

## Settings and source

Updates retain settings. Unchanged saves avoid a new configuration write; events and uptime stay in RAM. Exports omit AppKeys, Bluetooth PIN, WiFi password and network identities. Keep those separately.

The Arduino sketch is in [stm32](stm32/stm32.ino). [Build instructions](docs/DEVELOPMENT.md) cover firmware, the portal, installer and release packaging. Install only trusted firmware and maintain power throughout updates; there is no automatic rollback.

© 2026 Roel Broersma. Original code: [MIT](LICENSE). Dependencies retain their [own licenses](THIRD-PARTY-NOTICES.md). Independent project; not affiliated with Victron Energy, RAKwireless or Milesight.
