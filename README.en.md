# Victron LoRaBLE Remote

**Power your modem, router or other equipment only when you need it.**

[Nederlands](README.md) · [Download](https://github.com/roelbroersma/victron-lorable-remote/releases/latest) · [Installation](docs/INSTALL.md#english) · [LoRaWAN setup](docs/NETWORKS.md#english) · [Examples](docs/EXAMPLES.md)

LoRaBLE Remote receives a LoRaWAN command and operates a device through **Bluetooth or a relay**. Leave your 4G/5G modem off until you need remote access. A local voltage input can trigger the same functions.

Use a private LoRaWAN network — for example, a **Milesight UG63 / UG65** — or **The Things Network (TTN)**. Configure up to four networks with priority and automatic fallback. One network session is active at a time.

## Get started

1. Download the **Windows.zip** from the [latest release](https://github.com/roelbroersma/victron-lorable-remote/releases/latest) and extract everything.
2. Connect your **RAK11162** by USB and open **Install.cmd**. Follow the wizard.
3. Connect to WiFi **Victron LoRaBLE Remote**, password **CHANGE-ME-FIRST**, and open **http://192.168.4.1/**. Change the password immediately.
4. Select your device, add Bluetooth functions, enter LoRaWAN credentials and click **Save**.

First installation uses a Windows PC with USB and WiFi; the wizard handles the temporary WiFi connection. Later updates use **one complete .bin file**, through USB or **Manage → Update firmware**. Settings are retained. [Step-by-step installation →](docs/INSTALL.md#english)

## What can you control?

| Device | Functions |
|---|---|
| **Victron Smart MPPT** | Eight LOAD output operating modes |
| **Victron Smart BatteryProtect** | ON/OFF; 12/24V-100A, product A3B1 |
| **Generic Bluetooth** | Custom GATT service, characteristic and command bytes |
| **Relay / dry contact** | On, off or a timed pulse |

Create up to **ten named Bluetooth functions** for one target device. Assign them to LoRaWAN commands or rising/falling input edges. Enable only the actions you want to allow.

The English/Dutch web interface includes status, recent events, manual controls, an energy estimate and field help. WiFi can stay available for a configurable period after startup or a trigger.

<details>
<summary>See the interface: status, networks and management</summary>

![Status with example settings](docs/images/status.png)

![Network profiles and priority](docs/images/networks.png)

![Backup, restore, update and restart](docs/images/manage.png)

</details>

## Hardware

| Component | Purpose |
|---|---|
| **RAK11162** with RAK11160 module | LoRaWAN, WiFi and Bluetooth |
| **RAK19010 + RAK19012** | Baseboard with USB/LiPo/solar power and USB programming connection |
| **RAK19016**, alternative after installation | 5–24V power module |
| **RAK13001**, optional | One isolated 12–24V DC input and one relay output |
| **RAK13007**, optional | One relay output, no input |
| Antennas | LoRa and 2.4GHz |

Select the fitted I/O module yourself; LoRaWAN → Bluetooth also works without one. Use one power module per power slot.

RAK13001 detects **12–24V DC presence/absence**, not battery voltage. Check jumpers: input **WB_IO3**, relay **WB_IO4**. Never connect 12V directly to a processor pin. Relay contacts do not provide power; the relays consume coil current while energized.

## Send a command

Send **HEX** on the configured FPort, default **10**, and enable the corresponding downlink permissions.

| HEX | Action |
|---|---|
| `01` … `09`, `0A` | Bluetooth function 1 … 10 |
| `10` / `11` | Relay off / on |
| `12` | Relay pulse |
| `20` | Request status |

Function 10 is **`0A`**, not `10`. Use **Class C** on both device and network server to receive commands without waiting for an uplink. [Configure TTN, Milesight and the payload codec →](docs/NETWORKS.md#english)

## Good to know

- **Saving is explicit:** network reordering also requires Save. Events and uptime stay in RAM.
- **Backups exclude secrets:** retain AppKeys, Bluetooth PIN and WiFi password separately.
- **Energy:** savings come from equipment you can leave off. Class C listens almost continuously; it is not a microamp sleep mode. The interface estimates daily use in mAh or Wh.
- **Bluetooth:** WiFi pauses during commands. MPPT User defined/AES uses existing VictronConnect thresholds; the BatteryProtect profile accepts product A3B1 and leaves protection thresholds and BMS mode unchanged.

Power the board independently of its switched load and keep the gateway reachable while your modem is off. Use appropriate fuses and a local disconnect; this is not a safety controller. Maintain power throughout updates.

[Arduino sketch](stm32/stm32.ino) · [Building and source](docs/DEVELOPMENT.md#english) · [Help / issues](https://github.com/roelbroersma/victron-lorable-remote/issues)

© 2026 Roel Broersma · [MIT](LICENSE) · [Dependency licenses](THIRD-PARTY-NOTICES.md). Independent project; not affiliated with Victron Energy, RAKwireless or Milesight.
