# Power estimate / Stroomraming — 17 September 2026

This is a **datasheet-based planning model**, not a current measurement or a battery-life guarantee. The portal requires no manual entries. Defaults: Class C reception for 24 h, WiFi AP for 1 h, one 10-second Bluetooth transaction, relay comparisons at 0/1/24 h. This scenario does not change the saved LoRa class (the test board currently uses Class A).

## Reference model at the 3.3 V rail

| Component / state | Current used | Time/day | Charge/day | Basis |
| --- | ---: | ---: | ---: | --- |
| STM32WLE5, awake 48 MHz, SMPS | 3.45 mA | 24 h | 82.80 mAh | ST typical at 3 V/25°C, peripherals disabled; not a measured firmware average |
| LoRa RX, Class C | 4.82 mA | 24 h | 115.68 mAh | ST radio, boosted RX/125 kHz/SMPS |
| ESP8684 + WiFi AP | 68.05 mA | 1 h | 68.05 mAh | 99% × 65 mA RX + 1% × 370 mA TX, explicit illustrative duty cycle |
| ESP8684 + Bluetooth | 63.28 mA | 10 s | 0.176 mAh | 99% × 62 mA RX + 1% × 190 mA TX at +9 dBm |
| ESP disabled | 0.001 mA | 22 h 59 min 50 s | 0.023 mAh | Espressif typical CHIP_EN low |
| Released relay coil | 0 mA | 24 h | 0 mAh | Coil only; not a claim of zero complete I/O-board current |

**Partial subtotal without energized relay: about 267 mAh/day, 0.88 Wh/day.**

The hardware RX reference from RAK is 5.46 mA at module level. It is **not** added to ST's 4.82 mA radio current: that would count the receiver twice. External TCXO and additional peripheral loads are not characterized in this partial sum. Datasheet conditions differ from the full board; the numbers are indicative, not calibrated.

### WiFi and Bluetooth uncertainty

- WiFi's 65/370 mA figures are active-state/peak references, **not a measured AP average**. For the same model with 10% TX time, WiFi becomes 95.5 mA: +27.45 mAh for its one-hour window. This is a traffic sensitivity example, not guaranteed minimum/maximum consumption.
- The firmware uses SoftAP, 120 MHz ESP CPU, and does not enable ESP dynamic power management. Station-only modem-sleep values must not be substituted for this access point. Do not add a second ESP CPU current to the full ESP operating-state figures.
- Bluetooth's configured default TX power is +9 dBm, checked in `esp8684/build_native48/sdkconfig`. Discovery, connection, pairing and verification take time even for one logical command. A 10-second example is small (~0.18 mAh); retries and an absent device can make it longer. The SmartSolar is not present, so the actual transaction time is unmeasured.
- ESP is disabled by STM when neither a WiFi window nor a Bluetooth job requires it. The one-hour budget is an assumption: an active input or later triggers can extend real WiFi uptime.

## Relay modules

Both modules use ordinary **non-latching** relays. Energizing for a second and holding for a day are very different energy costs. The load switched by the contacts is excluded.

| Module | Identification | Energized current | 1 second | 1 hour | 24 hours |
| --- | --- | ---: | ---: | ---: | ---: |
| RAK13001 | RAK schematic: HF46F-G/3; user board HF46F | 70 mA, RAK module typical | 0.0194 mAh | 70 mAh | 1680 mAh |
| RAK13007 | RAK schematic: Omron G5LE-14-DC3 | ~142 mA, **inferred model** | 0.0394 mAh | 142 mAh | 3408 mAh |

RAK13001: Hongfa specifies 200 mW nominal coil power. At nominal 3 V this implies R≈3²/0.2=45 Ω. At a 3.3 V rail, using a 0.2 V transistor drop, the coil draws ~68.9 mA plus ~2.6 mA through the 1 kΩ transistor base resistor: close to RAK's 70 mA module reference. Use the published module number, not 200 mW/3.3 V. Rail voltage, transistor drop, coil tolerance and coil heating change the current.

RAK13007: the RAK drawing explicitly names a 3 V G5LE. The consulted current Omron G5LE datasheet lists 400 mW but only its 5/12/24 V variants. **The old 3 V coil is not separately specified in that primary table.** The model infers 22.5 Ω from 3²/0.4, then (3.3−0.2)/22.5≈137.8 mA, ~2.6 mA base drive and ~1.1 mA indicator LED current. Rounded: 142 mA. With ±10% resistance and 0.1–0.3 V transistor drop, this model suggests roughly 124–163 mA around room temperature, not a guaranteed limit. Verify the actual fitted part and measure before using it for a precise battery budget.

Portal partial totals:

- RAK13001: ~267 mAh/day released; ~337 mAh with 1 hour energized; ~1947 mAh with 24 hours energized.
- RAK13007: ~267 mAh/day released; ~409 mAh with 1 hour energized; ~3675 mAh with 24 hours energized.
- Relay-ON examples replace released hours, and never add a second relay module. Unknown I/O idle current is excluded from both.

## LoRa transmissions and Class C

The default table shows continuous RX without uplinks. For an illustrative 18-byte application payload plus 13-byte LoRaWAN overhead, SF12, BW125, CR4/5, explicit header, CRC, eight-symbol preamble and low-data-rate optimization:

`airtime = (8 + 4.25 + 43) × 2^12 / 125000 = 1.810432 s`

Using RAK's +14 dBm TX reference of 92 mA gives ~0.0463 mAh per uplink. In Class C, TX replaces RX, so incremental charge over continuous RX is `(92−4.82) × 1.810432 / 3600 ≈ 0.0438 mAh`. For 96 uplinks/day (every 15 minutes), about **+4.21 mAh/day**, before retries/joins/extra overhead. ADR, SF, power, regional channels and extra MAC bytes change this. Do not use peak TX current for all 24 hours.

Class C receives except while transmitting and relevant receive-window changes. It is not a microamp sleep mode. RAK's 2.8 µA specifies Stop 1 with ESP disabled/RTC disabled. Current STM firmware disables deep low-power mode for reliable LoRa handling. The RUI BSP runs at 48 MHz. ST lists 5.65 mA instead of 3.45 mA for the corresponding non-SMPS run reference (+52.8 mAh/day); peripheral clocks and temperature are further variables.

## Power modules / complete board

The inspected RAK schematics identify:

- RAK19012: SGM6036-ADJ 3.3 V converter, TP4054 charger, status/user LEDs and battery voltage divider. A converter's unloaded quiescent current is not the complete USB-powered board consumption.
- RAK19016: SGM61230 input converter followed by SGM6036-ADJ, TP4054 charger and additional resistor networks. SGM61230's typical quiescent current is 25 µA **at its input**, not an amount to add directly to the 3.3 V current column. Conversion efficiency varies with input voltage/load; both conversion stages and charging must be included for a 12 V budget.
- Base/core USB interface, TCXO, LEDs, charger operating state, optocoupler input current and regulator losses are not measured. No complete-board maximum or exact 12 V battery runtime is claimed.

Battery-side energy conversion requires `I_battery = P_3V3 / (V_battery × efficiency) + other battery-side loads`. Do not directly equate 3.3 V mAh with 12 V mAh. No settings or power estimates are logged periodically to flash.

## Primary sources

- [ST STM32WLE5 datasheet, main performances and run-current tables](https://www.st.com/resource/en/datasheet/stm32wle5jc.pdf)
- [RAK11160 current consumption and sleep conditions](https://docs.rakwireless.com/product-categories/wisduo/rak11160-module/datasheet/)
- [Espressif ESP8684, active/current tables](https://documentation.espressif.com/esp8684_datasheet_en.html)
- [Espressif: station/AP power-saving distinctions](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c2/api-guides/wifi-driver/wifi-performance-and-power-save.html)
- [RAK13001 datasheet and relay schematic](https://docs.rakwireless.com/product-categories/wisblock/rak13001/datasheet/)
- [Hongfa HF46F-G nominal coil power](https://www.hongfa.com/product/power-relay/HF46F-G)
- [RAK13007 datasheet and G5LE-14-DC3 schematic](https://docs.rakwireless.com/product-categories/wisblock/rak13007/datasheet/)
- [Omron G5LE family datasheet](https://components.omron.com/sites/default/files/datasheet_pdf/K100-E1.pdf)
- [RAK19012 schematic](https://images.docs.rakwireless.com/wisblock/rak19012/datasheet/rak19012-schematic.png)
- [RAK19016 schematic](https://images.docs.rakwireless.com/wisblock/rak19016/datasheet/rak19016-schematic.jpg)
- [SGMICRO SGM61230 datasheet](https://www.sg-micro.com/rect/assets/79f6e269-c98d-4e66-8d75-2091b3105498/SGM61230.pdf)
