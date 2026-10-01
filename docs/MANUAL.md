# Handleiding / User manual — 4.12.1

[README](../README.md) · [English](#english) · [Installatie](INSTALL.md) · [LoRaWAN-netwerken](NETWORKS.md)

## Nederlands

### Instellingen toepassen

De webinterface heeft de tabbladen **Status**, **Bluetooth**, **Ingangen & relais**, **LoRaWAN**, **WiFi** en **Beheer**. De vlaggen wisselen tussen Nederlands en Engels; het informatie-icoon bij een veld geeft uitleg.

Wijzigingen zijn eerst een concept. **Opslaan** bewaart ze en herstart het board; dit geldt ook voor een nieuwe netwerkvolgorde. Status verversen, een tabblad openen of een back-up downloaden slaat geen wijzigingen op. Handmatige tests gebruiken altijd de opgeslagen instellingen.

### WiFi en het adres van het board

Bij een eerste installatie is het accesspoint **Victron LoRaBLE Remote** beschikbaar met wachtwoord **CHANGE-ME-FIRST**. Verbind daarmee, open **http://192.168.4.1/** en wijzig het wachtwoord. **WiFi-naam accesspoint** bepaalt ook de paginatitel en de naam bij AP-terugval.

| WiFi-modus | Werking |
|---|---|
| Eigen accesspoint | Direct accesspoint op 192.168.4.1 zolang het WiFi-venster open is. |
| Eigen router + AP-terugval | Verbindt met je eigen 2,4GHz-WiFi en krijgt het adres via DHCP. Eerst geen AP; terugval komt alleen bij mislukking of verbindingsverlies. |
| WiFi uit | Geen WiFi, ook niet door een trigger of als de router wegvalt. Gebruik USB voor verdere configuratie of herstel. |

Voor routermodus vul je de **WiFi-naam eigen router** en het routerwachtwoord in. Het SSID heeft 1–32 gewone ASCII-tekens; het wachtwoord 8–63, inclusief eventuele spaties. Een leeg wachtwoordveld behoudt het opgeslagen wachtwoord. Het accesspoint heeft zijn eigen wachtwoord: 12–63 zichtbare ASCII-tekens zonder spaties.

Routermodus verloopt als volgt:

1. **Opstarttijd router:** standaard 90 seconden; instelbaar 0–600. 0 begint direct met verbinden. Tijdens deze eerste wachttijd is er geen AP.
2. **Verbinding proberen gedurende:** standaard 60 seconden; instelbaar 10–300. Het board probeert zowel een WiFi-verbinding als een DHCP-adres te krijgen. Ook deze eerste poging verloopt zonder AP.
3. Geen DHCP-verbinding binnen dat venster? Dan verschijnt **AP-terugval op 192.168.4.1** en volgen automatisch nieuwe routerpogingen. Terugval is alleen beschikbaar zolang het WiFi-venster open is.
4. Zodra DHCP lukt, toont de status **Connected**, het toegewezen adres en de router-RSSI. Het AP gaat uit, ook als er nog een AP-client verbonden was. Verbind met je router en open **het toegewezen IP-adres van dit board**, te vinden in de apparatenlijst van de router.
5. Verliest het board de routerverbinding of het DHCP-adres, dan komt AP-terugval terug en worden routerpogingen hervat.

192.168.4.1 is uitsluitend het adres van het directe accesspoint, **geen vast adres op je routernetwerk**. **Connected** bevestigt WiFi/DHCP, niet de bereikbaarheid van internet. WiFi-tijden na opstart, ingang en toegestane LoRa-opdrachten blijven van toepassing. Bluetooth-acties kunnen WiFi tijdelijk onderbreken.

### Bluetooth-functies

Kies één doelapparaat, stel MAC-adres en beveiliging in en maak maximaal tien benoemde functies. Schakel **Bluetooth Functies inschakelen** in en koppel de gewenste functies bij de ingang of bij LoRa-ontvangstrechten.

- **Smart MPPT:** kies een LOAD-regelmodus. De normale apparaatinstantie is **3**; wijzig deze geavanceerde waarde alleen met passende protocolinformatie. User defined en AES gebruiken de bestaande drempels uit VictronConnect.
- **Smart BatteryProtect:** kies **12/24V-100A (A3B1)** of **48V-100A (A3B3)** en maak AAN/UIT-functies. Een niet-passende productcode wordt vóór schakelen geweigerd. De driver gebruikt intern instantie 0 zonder de opgeslagen MPPT-instantie te overschrijven. Een actieve beveiliging kan inschakelen tegenhouden.
- **Generic:** gebruik de gedocumenteerde GATT-service, characteristic en 1–20 opdrachtbytes van het apparaat. Exact teruglezen vereist dat het kenmerk die waarde kan teruggeven.

Een scanresultaat **gevonden** bewijst geen geslaagde opdracht. De Bluetooth-status toont het laatste opdrachtresultaat; bij een fout ook de stap en beschikbare detailcode. MPPT- en BatteryProtect-bediening lezen modus en uitgangsstatus terug; bij geforceerd AAN/UIT moet de uitgang bij de gevraagde toestand passen. **Always ON** schakelt de onderspanningsafschakeling van de betreffende MPPT-LOAD-regel uit.

### Ingangsflanken en wachttijden

Selecteer de gemonteerde I/O-module en schakel de ingang in. Alleen RAK13001 heeft de hier gebruikte ingang: **12–24V DC aanwezig/afwezig**, geen spanningsmeting. Controleer de bedrading/routing naar WB_IO3; sluit nooit 12V rechtstreeks op een processorpin aan. Het relais gebruikt WB_IO4.

Per flank kies je een LoRa-bericht, een Bluetooth-functie en/of een relaisactie. De twee timingvelden werken op diezelfde acties:

| Veld | Betekenis |
|---|---|
| Minimale AAN-tijd / Minimale UIT-tijd | Hoelang de ingang onafgebroken in de nieuwe toestand moet blijven. 0 = geen extra filtertijd. |
| Actievertraging | Extra wachttijd daarna, met dezelfde ingangstoestand. 0 = geen extra vertraging. |

Beide velden zijn onafhankelijk per flank instelbaar van **0–3600 seconden**, standaard 0. Bijvoorbeeld **5 + 10 = 15 seconden** onafgebroken AAN. Een tegengestelde flank vóór uitvoering annuleert de wachtende actie en start de eventueel ingestelde timing voor die andere flank. Aanhouden van een toestand herhaalt de actie niet.

Beide waarden 0 betekent geen extra wachttijd na de **80ms-debounce**. Opstarten telt niet als flank. De gekozen acties worden eenmaal geactiveerd; hun uitvoering kan daarna afhankelijk zijn van Bluetooth of radioverkeer. **Test opgaande/neergaande flank** test de acties direct, zonder deze fysieke timing en zonder de ingang of flanktellers te wijzigen.

Houd het board gevoed als de bewaakte spanning of geschakelde belasting uitvalt. Relaisstatus is de aangestuurde stand, geen meting van het contact.

### LoRa-vermogen en diagnose

Stel in **LoRaWAN** het frequentieplan, netwerkprofielen en de gewenste Class in. Gebruik Class C op board én server voor ontvangst zonder wachten op een eigen uplink; zie de [netwerkhandleiding](NETWORKS.md).

**Maximaal LoRa-zendvermogen** is een opgeslagen bovengrens van **0–22 dBm**, standaard **14 dBm**. **0 dBm = 1 mW; dit schakelt de radio niet uit.** ADR blijft instelbaar en kan het vermogen verlagen; ook regionale of onderhandelde netwerkbeperkingen kunnen een lagere waarde opleveren. De status toont het laatst ingestelde radiovermogen, geen vermogensmeting. Antennewinst telt mee voor de uitgestraalde EIRP: kies een waarde die past bij de toepasselijke beperkingen, niet automatisch het maximum.

| Status | Wat deze wel en niet bevestigt |
|---|---|
| Radio-aanvraag geaccepteerd | De radiostack heeft de aanvraag aangenomen; nog geen bewijs van verzending of ontvangst. |
| Lokale TX voltooid | Lokale zendafhandeling geslaagd; de teller telt geen gegarandeerde netwerkafleveringen. |
| ACK ontvangen | Een netwerkbevestiging voor de bevestigde uplink. Geen ACK bewijst niet dat de uplink verloren ging. |
| Laatste geldige ontvangst | RSSI/SNR van een ontvangen bericht, met profiel en ouderdom. Niet de uplink-RSSI zoals een gateway die meet. |
| Aanmeldresultaat / MAC-code | Radioresultaat van de aanvraag of aanmelding. Een geweigerde aanvraag is iets anders dan een voltooide join. |

`—` betekent dat er nog geen beschikbare waarde is. Een mislukte join levert geen fictieve RSSI op. Een oudere ontvangst kan bij een ander profiel horen; controleer daarom naam en ouderdom. Bij een verbroken browserverbinding blijven de laatst ontvangen waarden zichtbaar als verouderde status.

### Handmatig testen

Onder **Status** staan de handmatige knoppen. Ze gebruiken opgeslagen instellingen en kunnen echte uitgangen schakelen.

- **Nu verzenden (ACK):** vraagt één statusbericht met netwerkbevestiging aan, zonder het periodieke statusinterval af te wachten. Er is een actieve LoRaWAN-sessie nodig voordat het bericht kan worden verzonden.
- **Nu aanmelden:** vraagt een nieuwe OTAA-aanmelding aan op het hoogste geldige, ingeschakelde voorkeursprofiel. Een lopende radioactie wordt eerst afgerond; de preempt-tijd hoeft niet te verlopen.
- **Bluetooth-/flank-/relaistests:** gebruiken de opgeslagen functie of actie. Sla gewijzigde regels eerst op. Een flanktest slaat de fysieke minimale tijd en actievertraging over.

Een melding **aangevraagd** bevestigt alleen dat de opdracht is aangenomen voor verdere afhandeling. Kijk daarna naar radioacceptatie, lokale TX, ACK of het Bluetooth-resultaat. Log vernieuwen houdt deze testmelding gescheiden van scanresultaten.

De handmatige LoRa-knoppen in deze release zijn diagnostische eenmalige aanvragen: zij kunnen normale TTN-aanmeldbudgetten en radio-duty-cycle-/wachttijden overslaan. Automatisch verkeer behoudt zijn gewone beleid. Gebruik deze knoppen spaarzaam en houd rekening met toegestane radiozendtijd en netwerkregels; een handmatige aanvraag garandeert geen verbinding en verruimt geen toestemming om uit te zenden.

### Back-up, herstel en bijwerken

**Beheer → Back-up** exporteert opgeslagen instellingen, inclusief WiFi-modus, vermogensgrens en ingangstijden. Conceptwijzigingen, wachtwoorden, AppKeys, Bluetooth-PIN en DevEUI gaan niet mee. Bewaar geheimen apart; lege geheimvelden behouden wat op hetzelfde board is opgeslagen. Op een nieuw board moet je ze opnieuw invullen.

Een oudere back-up krijgt standaardwaarden voor de nieuwe velden: AP-modus, maximaal 14 dBm, router-opstarttijd 90 s, verbindingsvenster 60 s en alle ingangstijden 0. Controleer instellingen en behouden sleutels vóór herstel. Herstellen vraagt bevestiging, slaat op en herstart het board. LoRaWAN-beveiligingstellers zijn geen onderdeel van de configuratieback-up.

Updates gebruiken **één compleet LoRaBLE .bin-bestand**, via USB of **Beheer → Firmware bijwerken**. Gebruik het bestand van de gekozen release, laat de voeding aangesloten en volg de [installatiehandleiding](INSTALL.md). Instellingen blijven behouden; een herstart laat het relais afvallen. Voor de eerste installatie gebruik je het volledige Windows.zip-pakket en **Install.cmd**.

Voed het board onafhankelijk van de geschakelde belasting, houd de gateway bereikbaar en gebruik passende zekeringen en een lokale uitschakelmogelijkheid. Dit is geen veiligheidscontroller.

## English

[English README](../README.en.md) · [Installation](INSTALL.md#english) · [LoRaWAN networks](NETWORKS.md#english)

### Applying settings

The interface has **Status**, **Bluetooth**, **Inputs & relay**, **LoRaWAN**, **WiFi** and **Manage** tabs. Flags select English/Dutch; each information icon explains its field.

Edits remain drafts until **Save**, which stores the settings and restarts the board. Network reordering also requires Save. Status refresh, tab changes and backup downloads never save drafts. Manual tests always use saved settings.

### WiFi and the board's address

After first installation, connect to **Victron LoRaBLE Remote**, password **CHANGE-ME-FIRST**, open **http://192.168.4.1/** and change the password. **Access-point WiFi name** also sets the page title and fallback AP name.

| WiFi mode | Behavior |
|---|---|
| Device access point | Immediate AP at 192.168.4.1 while the WiFi window is open. |
| Own router + AP fallback | Uses your own 2.4 GHz WiFi and a DHCP-assigned address. Initially no AP; fallback starts only after failure or connection loss. |
| WiFi off | No WiFi, including on triggers or router loss. Use USB for further configuration or recovery. |

Router SSID: 1–32 printable ASCII characters. Router password: 8–63, including spaces; blank retains the saved password. The AP has a separate password of 12–63 printable ASCII characters without spaces.

Router mode works as follows:

1. **Router startup time:** default 90 seconds, range 0–600; 0 attempts connection immediately. There is no AP during this initial wait.
2. **Connection attempt window:** default 60 seconds, range 10–300. The board attempts association and DHCP, still without an initial AP.
3. If no DHCP connection is obtained within that window, **AP fallback starts at 192.168.4.1** and router retries continue automatically. Fallback is available only while the WiFi window remains open.
4. Once DHCP succeeds, status shows **Connected**, the assigned address and router RSSI. AP switches off, even if an AP client was connected. Join your router's network and open **the IP address assigned to this board**, found in the router's device list.
5. Losing the router connection or DHCP address brings AP fallback back and resumes retries.

192.168.4.1 belongs to the direct AP; it is **not a static fallback address on your router network**. **Connected** confirms WiFi/DHCP, not internet availability. Startup, input and permitted LoRa-triggered WiFi windows still apply. Bluetooth actions can temporarily interrupt WiFi.

### Bluetooth functions

Select one target device, configure its MAC address/security and create up to ten named functions. Enable Bluetooth functions, then assign the desired input triggers or LoRa permissions.

- **Smart MPPT:** select a LOAD control mode. Normal device instance is **3**; change this advanced value only with appropriate protocol information. User defined/AES uses existing VictronConnect thresholds.
- **Smart BatteryProtect:** select **12/24V-100A (A3B1)** or **48V-100A (A3B3)** and create ON/OFF functions. A mismatched product ID is refused before switching. Its driver uses instance 0 internally without overwriting the MPPT setting. Active protection may prevent switching on.
- **Generic:** use the device's documented GATT service, characteristic and 1–20 command bytes. Exact readback requires that characteristic to expose the expected value.

Finding a device in a scan does not prove successful control. Bluetooth status reports the last command result, plus stage/detail on failure. MPPT and BatteryProtect control reads back mode and output state; forced ON/OFF also requires the expected output. **Always ON** disables the low-voltage cutoff of that MPPT LOAD control rule.

### Input edges and timing

Select the installed I/O module and enable its input. RAK13001 detects **12–24V DC presence/absence**, not voltage magnitude. Check WB_IO3 input routing and WB_IO4 relay wiring; never connect 12V directly to a processor pin.

Each edge can request a LoRa message, a Bluetooth function and/or a relay action. Its timing fields apply to those same actions:

| Field | Meaning |
|---|---|
| Minimum ON / OFF duration | Continuous time in the new input state. 0 adds no filter time. |
| Action delay | Additional wait after that duration, still requiring the same input state. 0 adds no delay. |

Each field is independently set per edge from **0–3600 seconds**, default 0. **5 + 10 = 15 seconds** continuously ON, for example. An opposite edge cancels the pending action and starts that other edge's configured timing. Holding a level does not repeat the action.

Both values at 0 add no wait after **80ms debounce**. Startup is not an edge. Selected actions are activated once; subsequent execution depends on Bluetooth/radio handling. **Test rising/falling edge** runs actions immediately, bypassing this physical timing without changing the input or edge counters.

Keep the board powered when the monitored voltage or switched load goes off. Relay status reports the commanded state, not contact feedback.

### LoRa power and diagnostics

Set frequency plan, network profiles and Class under **LoRaWAN**. Use Class C on both device and server for reception without waiting for an uplink; see the [network guide](NETWORKS.md#english).

**Maximum LoRa transmit power** is a saved **0–22 dBm** ceiling, default **14 dBm**. **0 dBm = 1 mW; it does not switch the radio off.** ADR remains selectable and may lower output, as may regional or negotiated network restrictions. Status shows the last configured radio output, not a measurement. Antenna gain contributes to radiated EIRP; select a value appropriate to applicable restrictions, not automatically the maximum.

| Status | What it confirms |
|---|---|
| Radio request accepted | The radio stack accepted the request; no proof yet of transmission or reception. |
| Local TX complete | Successful local transmit completion, not guaranteed network delivery. |
| ACK received | Network acknowledgement of the confirmed uplink. No ACK does not prove the uplink was lost. |
| Last valid reception | Received-message RSSI/SNR with profile and age, not your uplink's gateway-measured RSSI. |
| Join result / MAC code | Request or join radio result. A rejected request is distinct from a completed join. |

`—` means no value is available yet. Failed joins do not invent RSSI. Old receptions can belong to another profile; check name and age. If the browser disconnects, its last received values remain visible as stale status.

### Manual tests

The controls under **Status** use saved settings and can operate real outputs.

- **Send now (ACK):** requests one confirmed status message without waiting for the periodic interval. An active LoRaWAN session is required before transmission.
- **Join now:** requests fresh OTAA on the highest valid enabled priority profile, without waiting for preemption. An active radio operation finishes first.
- **Bluetooth/edge/relay tests:** use saved functions/actions. Save edits first. Edge tests bypass the physical minimum duration and action delay.

**Requested** confirms only acceptance for further handling. Check subsequent radio acceptance, local TX, ACK or Bluetooth result. Refreshing the log keeps test feedback separate from scan results.

This release's manual LoRa controls are diagnostic one-shot requests: they can bypass ordinary TTN join budgets and radio duty-cycle/backoff waits. Automatic traffic retains its normal policy. Use these controls sparingly and respect permitted airtime and network rules; a manual request guarantees neither connectivity nor permission to transmit.

### Backup, restore and updates

**Manage → Backup** exports saved settings, including WiFi mode, power ceiling and input timing. Draft edits, passwords, AppKeys, Bluetooth PIN and DevEUI are excluded. Store secrets separately; blank secret fields retain values on the same board. Enter them again on a replacement board.

Older backups receive defaults for the new fields: AP mode, 14 dBm maximum, 90-second router startup, 60-second connection window and zero input times. Check settings and retained keys before restoring. Restore requires confirmation, saves and restarts the board. LoRaWAN security counters are separate from configuration backups.

Updates use **one complete LoRaBLE .bin file**, through USB or **Manage → Update firmware**. Use the chosen release's file, maintain power and follow the [installation guide](INSTALL.md#english). Settings are retained; restarting releases the relay. First installation uses the complete Windows.zip package and **Install.cmd**.

Power the board independently of its switched load, keep the gateway reachable and provide appropriate fuses and a local disconnect. This is not a safety controller.
