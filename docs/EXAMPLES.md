# Voorbeelden / Examples

## 1. Modem op afstand wakker maken / Wake a cellular modem

**NL.** Voed de RAK altijd via een apart gezekerde voeding. Zet het modem achter
een daarvoor geschikte BatteryProtect of de MPPT-LOAD-uitgang. Maak functie 1
`Modem aan` en functie 2 `Modem uit`; kies de profielacties AAN en UIT. Geef beide
functies LoRa-toestemming. Gebruik Class C op node én server. Downlink `01` start
het modem; wacht op de uitvoeringsstatus en vervolgens op zijn netwerkverbinding.
Downlink `02` schakelt het weer uit. Maak geen lus waarbij de enige LoRa-gateway
internet nodig heeft via precies het modem dat je uitschakelt.

**EN.** Power the node independently through a fuse. Put the modem behind a
suitable BatteryProtect or MPPT LOAD output. Functions 1/2 are modem ON/OFF;
permit both in LoRa settings and use Class C at both ends. Send hex `01`, wait
for verified action status and then the modem's own connection. Send `02` to
turn it off. Never depend on that same switched modem for your only gateway's
backhaul.

## 2. Lokale spanning als eerste trigger / Local voltage trigger

**NL.** Gebruik de RAK13001-ingang voor een geschikt 12–24V DC-signaal.
Selecteer de module en zet de ingang aan. Opgaande flank: functie `Modem aan`
plus `Verstuur LoRa-status`. Neergaande flank: desgewenst `Modem uit` plus status.
Laat WiFi één uur na een flank beschikbaar, of houd het aan zolang het signaal
actief is. De ingang detecteert spanning; hij bewijst niet dat een dynamo draait
en is geen voltmeter. Geen directe aansluiting op onbekende automotive pieken.

**EN.** Use a suitable 12–24V DC signal on RAK13001's isolated input. Rising:
modem ON plus send status. Falling: optionally modem OFF plus status. Configure
WiFi hold time after an edge or while active. This detects voltage presence,
not engine operation or battery voltage. Do not feed unknown automotive
transients directly into the input.

## 3. Droog contact / Dry contact

**NL.** Selecteer RAK13001 of RAK13007, schakel de uitgang in en geef de gewenste
LoRa-acties toestemming. `11` sluit het aangestuurde relais, `10` laat het afvallen,
`12` geeft de ingestelde puls. Sluit NO/NC/COM aan volgens de module, niet volgens
een softwarelabel. De status meldt de aangestuurde stand, geen contactmeting.
Beide relais verbruiken continu spoelstroom zolang ze aangetrokken zijn.

**EN.** Select the installed relay module and enable the output and permitted
downlink actions. Hex `11` energizes, `10` releases, `12` pulses for the configured
duration. Wire NO/NC/COM according to the hardware. Status is the commanded
state, not contact feedback. Both supported relay modules are non-latching.

## 4. Ander Bluetooth-apparaat / Other Bluetooth device

**NL.** Kies Generic, vul MAC/adrestype in en gebruik uitsluitend het protocol
van de fabrikant. Per functie: service-UUID, characteristic-UUID en 1–20 bytes
hexdata. Kies teruglezen alleen als hetzelfde kenmerk de verwachte waarde kan
teruggeven. Gebruik geen voorbeeld-UUID voor een echt apparaat: de juiste bytes
hangen volledig van dat apparaat af.

**EN.** Choose Generic and the target MAC/address type. Supply the manufacturer's
service UUID, characteristic UUID and 1–20 hex bytes per function. Exact readback
requires that same characteristic to expose the expected value. No arbitrary
scripts, write-without-response or notification-based custom protocol engine is
included. Do not guess commands for an unknown device.

## 5. Eigen netwerk met TTN als reserve / Private network with TTN backup

**NL.** Zet je eigen netwerk bovenaan (bijvoorbeeld Milesight UG63 / UG65) en TTN tweede. Geef beide hun eigen JoinEUI en
AppKey; kies voor TTN het type TTN Sandbox en laat RX2 op automatisch. Zet de
preempt-tijd van TTN op 1440 minuten. Als de eerste aanmelding mislukt, blijft
het board een dag bij TTN: verbonden of wachtend op een toegestane nieuwe poging.
Daarna probeert het de eigen server opnieuw, zonder het board te herstarten.
Wissel de volgorde met ↑/↓ als je TTN juist als voorkeur wilt. Registreer het
end device op beide servers; alleen een aangesloten gateway is niet voldoende.

**EN.** Put your private network first (for example, Milesight UG63 / UG65) and TTN second, with independent JoinEUI/AppKey
tuples. Use TTN Sandbox type, automatic RX2 and 1440-minute backup preemption.
After a failed preferred join the node stays on TTN for a day, joined or waiting
for an allowed retry. It then tries the private network again without an MCU
reboot. Reverse priority with ↑/↓ if TTN should be preferred. Register the end
device on each server; gateway registration alone does not register the node.

Send commands through the currently active network. Remove obsolete commands
from server queues: the simple function-command payload contains no expiry time.
Only one network session is active, and rejoining temporarily interrupts reception.
