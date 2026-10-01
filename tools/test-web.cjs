// Isolated mock portal; never sends commands to physical hardware.
const fs=require('node:fs'),path=require('node:path'),http=require('node:http'),assert=require('node:assert/strict');
const {chromium}=require(process.env.PLAYWRIGHT_PATH||'playwright');
const root=path.resolve(__dirname,'..'),html=fs.readFileSync(path.join(root,'web/index.html'));
const limits=fs.readFileSync(path.join(root,'stm32/portal_limits.h'),'utf8');
const maxBody=Number(limits.match(/#define LORABLE_HTTP_BODY_MAX (\d+)/)[1]);
assert.equal(maxBody,7800);
assert.match(fs.readFileSync(path.join(root,'stm32/legacy_at_portal.cpp'),'utf8'),/bodyUsed \+ n > LORABLE_HTTP_BODY_MAX/);
assert.match(fs.readFileSync(path.join(root,'esp8684/main/portal.c'),'utf8'),/#define HTTP_BODY_MAX LORABLE_HTTP_BODY_MAX/);
const config={language:0,wifi_ssid:'Victron LoRaBLE Remote',profile:3,load_enabled:1,victron_mac:'AA:BB:CC:DD:EE:FF',address_type:-1,instance:0,smp:1,window_min:60,status_min:15,ble_attempts:3,region:4,class:0,fport:10,subband:0,adr:1,dev_eui:'0123456789ABCDEF',join_eui:'0123456789ABCDEF',rx2_custom:0,rx2_freq:869525000,rx2_dr:0,ble_available:1,joined:1,ble_result:1,revision:7,input_enabled:1,relay_enabled:1,rising_actions:5,falling_actions:1,relay_pulse_ms:1000,io_board:1,wifi_triggers:3,wifi_input_min:60,wifi_lora_min:60,downlink_allowed:28,function_count:2,rising_fn:1,falling_fn:0,downlink_functions:0};
const newDefaults={tx_dbm:14,wifi_mode:0,sta_ssid:'',sta_delay_s:90,sta_timeout_s:60,rise_hold_s:0,rise_delay_s:0,fall_hold_s:0,fall_delay_s:0};
Object.assign(config,newDefaults,{sta_password_set:true});
for(let i=1;i<=10;i++)Object.assign(config,{['fn'+i+'_name']:'Function '+i,['fn'+i+'_kind']:i<3?10+i:0,['fn'+i+'_service']:'',['fn'+i+'_char']:'',['fn'+i+'_value']:''});
config.network_health_min=240;config.network_slot=0;config.network_state=3;
for(let i=0;i<4;i++)Object.assign(config,{['net'+i+'_name']:['Milesight UG63 / UG65','TTN','Network 3','Network 4'][i],['net'+i+'_enabled']:i<2?1:0,['net'+i+'_kind']:i===1?1:0,['net'+i+'_key_set']:i<2?1:0,['net'+i+'_join_eui']:i?'000000000000000'+i:'0123456789ABCDEF',['net'+i+'_rx2_custom']:0,['net'+i+'_rx2_freq']:869525000,['net'+i+'_rx2_dr']:0,['net'+i+'_preempt_min']:1440,['net'+i+'_order']:i});
let saved,saveCode='saved',allowAction=true,acceptDialog=true,generation=0,loraStatus={};
let wifiSession={wifi_phase:'ap',wifi_ap_active:true};
let storedStaPassword='previous router password';
let logEntries=[[0,1,0],[3,9,1]];
const writes=[],initialConfig=JSON.parse(JSON.stringify(config));
const server=http.createServer((req,res)=>{
 res.setHeader('Content-Type','application/json');
 if(req.url==='/session')res.end(JSON.stringify({token:'test-token'+generation,native:true,esp_firmware:'4.12.1',wifi_clients:1,wifi_rssi_dbm:[-42],ble_target_mac:config.victron_mac,ble_target_state:1,ble_target_age_s:5,...wifiSession}));
 else if(req.url==='/config')res.end(JSON.stringify(config));
 else if(req.url==='/status')res.end(JSON.stringify({...config,...loraStatus,firmware:'4.12.1',uptime_s:generation?12:4567,tx_count:3}));
 else if(req.url==='/log')res.end(JSON.stringify({entries:logEntries}));
 else if(req.url==='/save'||req.url==='/action'||req.url==='/ota'){
  assert.equal(req.headers['x-lorable'],'test-token'+generation);const chunks=[];req.on('data',d=>chunks.push(d));req.on('end',()=>{
   const body=Buffer.concat(chunks);writes.push({route:req.url,body});saved=new URLSearchParams(body.toString());
   const ok=req.url==='/save'?saveCode!=='invalid_settings':req.url==='/action'?allowAction:false;
   res.statusCode=ok?200:400;res.end(JSON.stringify({ok,code:req.url==='/save'?saveCode:ok?'queued':'unavailable'}));
   if(ok&&req.url==='/action'&&saved.get('command')==='reboot')generation++;
   if(ok&&req.url==='/save'&&saveCode==='saved'){
    for(const key of Object.keys(config))if(saved.has(key))config[key]=typeof config[key]==='number'?Number(saved.get(key)):saved.get(key);
    if(saved.get('sta_password'))storedStaPassword=saved.get('sta_password');
    config.revision++;generation++;
   }
  });
 }
 else{res.setHeader('Content-Type','text/html');res.end(html);}
});
(async()=>{
 await new Promise(r=>server.listen(0,'127.0.0.1',r));let browser;
 try{
 browser=await chromium.launch({executablePath:process.env.CHROME_PATH||(process.platform==='win32'?'C:/Program Files/Google/Chrome/Application/chrome.exe':undefined),headless:true});
 const page=await browser.newPage({viewport:{width:1100,height:950}}),errors=[];page.on('pageerror',e=>errors.push(e.message));page.on('dialog',d=>acceptDialog?d.accept():d.dismiss());
 await page.goto('http://127.0.0.1:'+server.address().port);await page.waitForFunction(()=>typeof ready!=='undefined'&&ready);
 await page.waitForFunction(()=>document.getElementById('wifi_value').textContent==='Connected');
 assert.equal(await page.locator('#firmware_badge').textContent(),'LoRaBLE Remote · v4.12.1');
 assert.equal(await page.locator('#ota_file').getAttribute('accept'),'.bin');
 assert.match(await page.locator('#power_total').textContent(),/267 mAh/);assert.match(await page.locator('#power_total').textContent(),/1947 mAh/);
 // Missing diagnostics and zero-filled boot values must never invent RX or ACK.
 for(const id of ['request','tx','ack','power','join'])assert.equal(await page.locator('#lora_'+id+'_value').textContent(),'—');
 assert.equal(await page.locator('#lora_rx_value').textContent(),'RSSI — · SNR —');
 assert.match(await page.locator('#lora_detail').textContent(),/Lokale TX: 3/);
 const unknownLora={lora_tx_seq:0,lora_tx_state:0,lora_tx_code:0,lora_tx_ack:0,lora_tx_age_s:0,lora_tx_slot:0,lora_ack_count:0,lora_rx_seen:0,lora_rssi_dbm:0,lora_snr_db:0,lora_rx_age_s:0,lora_rx_slot:0,lora_join_seen:0,lora_join_code:0,lora_join_request_code:-1,lora_join_age_s:0,lora_join_slot:0,lora_tx_dbm:-128,lora_pending:0,lora_join_pending:0};
 const showLora=async fields=>{loraStatus={...unknownLora,...fields};await page.evaluate(()=>refreshStatus());};
 const originalForm=await page.evaluate(()=>formSignature()),originalSaved=await page.evaluate(()=>JSON.stringify(current));
 await showLora({lora_tx_dbm:10});
 assert.equal(await page.locator('#lora_power_value').textContent(),'10 dBm');
 assert.match(await page.locator('#lora_power_detail').textContent(),/Opgeslagen maximum: 14 dBm/);
 assert.equal(await page.locator('#adr').isEnabled(),true);assert.equal(await page.inputValue('#adr'),'1');
 assert.equal(await page.evaluate(()=>serialize().get('adr')),'1');assert.equal(await page.evaluate(()=>portable(current).adr),1);
 assert.equal(await page.evaluate(()=>formSignature()),originalForm);assert.equal(await page.evaluate(()=>JSON.stringify(current)),originalSaved);
 await page.click('#nav_lora');assert.equal(await page.locator('#adr').isEnabled(),true);assert.equal(await page.locator('#adr_override').count(),0);
 assert.equal(await page.locator('#tx_dbm').getAttribute('type'),'range');assert.equal(await page.inputValue('#tx_dbm'),'14');
 for(const value of [0,22]){await page.locator('#tx_dbm').evaluate((el,value)=>{el.value=value;el.dispatchEvent(new Event('input',{bubbles:true}));},String(value));assert.equal(await page.locator('#tx_dbm_value').textContent(),value+' dBm');assert.equal(await page.evaluate(()=>serialize().get('tx_dbm')),String(value));}
 await showLora({lora_tx_dbm:8,lora_max_dbm:20});assert.equal(await page.inputValue('#tx_dbm'),'22'); // Status must not rewrite a draft ceiling.
 assert.match(await page.locator('#lora_power_detail').textContent(),/Opgeslagen maximum: 20 dBm/);
 await page.evaluate(()=>applyFields(current));
 await page.click('#nav_status');
 await showLora({lora_pending:1,lora_join_pending:1});
 assert.equal(await page.locator('#lora_request_value').textContent(),'Wacht op radio');
 assert.equal(await page.locator('#lora_tx_value').textContent(),'—');assert.equal(await page.locator('#lora_ack_value').textContent(),'—');
 assert.match(await page.locator('#lora_pending_note').textContent(),/Handmatige aanmelding/);
 const attempt={lora_tx_seq:7,lora_tx_slot:0,lora_tx_age_s:12,lora_tx_dbm:14};
 await showLora({...attempt,lora_tx_state:1,lora_tx_ack:1});
 assert.equal(await page.locator('#lora_request_value').textContent(),'Geaccepteerd door radio');
 assert.equal(await page.locator('#lora_tx_value').textContent(),'Wacht op afronding');
 assert.equal(await page.locator('#lora_ack_value').textContent(),'Wacht op ACK');
 assert.match(await page.locator('#lora_request_detail').textContent(),/#7 · Profiel 1: Milesight UG63 \/ UG65 · 0d 0h 0m 12s geleden/);
 await showLora({...attempt,lora_tx_state:3,lora_tx_code:-8});
 assert.equal(await page.locator('#lora_request_metric').getAttribute('data-state'),'error');
 assert.match(await page.locator('#lora_request_detail').textContent(),/MAC -8/);
 assert.equal(await page.locator('#lora_tx_value').textContent(),'Niet gestart');
 await showLora({...attempt,lora_tx_state:4,lora_tx_code:-6,lora_tx_ack:3,lora_join_seen:1,lora_join_code:-3,lora_join_slot:0,lora_join_age_s:30,lora_rssi_dbm:-51,lora_snr_db:8});
 assert.equal(await page.locator('#lora_tx_value').textContent(),'TX/RX-afrondingsfout');
 assert.match(await page.locator('#lora_tx_detail').textContent(),/MAC -6/);
 assert.equal(await page.locator('#lora_join_value').textContent(),'MAC -3');
 assert.equal(await page.locator('#lora_rx_value').textContent(),'RSSI — · SNR —'); // Failed joins are not RX measurements.
 await showLora({lora_join_request_code:0});
 assert.equal(await page.locator('#lora_join_value').textContent(),'Aanvraag geaccepteerd');
 assert.match(await page.locator('#lora_join_detail').textContent(),/Nog geen aanmeldresultaat/);
 await showLora({lora_join_request_code:5,lora_join_seen:1,lora_join_code:-3,lora_join_slot:1,lora_join_age_s:60});
 assert.equal(await page.locator('#lora_join_value').textContent(),'Aanvraag geweigerd');assert.equal(await page.locator('#lora_join_metric').getAttribute('data-state'),'error');
 assert.match(await page.locator('#lora_join_detail').textContent(),/Aanvraag MAC 5 · Laatste resultaat: MAC -3/);
 assert.match(await page.locator('#lora_join_detail').textContent(),/Profiel 2: TTN \(ander profiel\)/);
 await showLora({...attempt,lora_tx_state:2,lora_tx_ack:0});
 assert.equal(await page.locator('#lora_tx_value').textContent(),'TX voltooid');assert.equal(await page.locator('#lora_ack_value').textContent(),'Niet gevraagd');
 await showLora({...attempt,lora_tx_state:2,lora_tx_ack:3});
 assert.equal(await page.locator('#lora_ack_value').textContent(),'Geen ACK ontvangen');
 assert.match(await page.locator('#lora_delivery_note').textContent(),/geen ACK bewijst niet/);
 await showLora({...attempt,lora_tx_state:2,lora_tx_ack:2,lora_ack_count:4,lora_rx_seen:1,lora_rssi_dbm:-114,lora_snr_db:-8,lora_rx_slot:1,lora_rx_age_s:90061});
 assert.equal(await page.locator('#lora_ack_value').textContent(),'ACK ontvangen');assert.equal(await page.locator('#lora_ack_metric').getAttribute('data-state'),'ok');
 assert.equal(await page.locator('#lora_rx_value').textContent(),'RSSI -114 dBm · SNR -8 dB');
 assert.match(await page.locator('#lora_rx_detail').textContent(),/Profiel 2: TTN \(ander profiel\) · 1d 1h 1m 1s geleden/);
 assert.match(await page.locator('#lora_ack_detail').textContent(),/ACKs sinds start: 4/);
 assert.equal(await page.evaluate(()=>formSignature()),originalForm);assert.equal(writes.length,0);
 await page.click('#lang_en');
 assert.equal(await page.locator('#lora_ack_value').textContent(),'ACK received');
 assert.match(await page.locator('#lora_rx_detail').textContent(),/other profile/);assert.match(await page.locator('#lora_power_detail').textContent(),/Saved maximum: 14 dBm/);
 assert.equal(await page.locator('#test_uplink').textContent(),'Send now (ACK)');assert.equal(await page.locator('#test_join_now').textContent(),'Join now');
 await page.click('#lang_nl');
 await showLora({lora_tx_dbm:14});assert.equal(await page.locator('#adr').isEnabled(),true);assert.equal(await page.locator('#lora_power_value').textContent(),'14 dBm');
 assert.equal(await page.evaluate(()=>formSignature()),originalForm);
 loraStatus={};await page.evaluate(()=>refreshStatus());
 // Router setup is a draft; secrets stay write-only and zero means immediate startup.
 await page.click('#nav_wifi');assert.equal(await page.inputValue('#wifi_mode'),'0');
 assert.equal(await page.locator('#label_wifi_ssid').textContent(),'WiFi-naam accesspoint');
 assert.equal(await page.locator('#field_sta_ssid').isVisible(),false);
 await page.selectOption('#wifi_mode','1');assert.equal(await page.locator('#field_sta_ssid').isVisible(),true);
 assert.match(await page.locator('#wifi_mode_note').textContent(),/AP-terugval op 192\.168\.4\.1/);
 assert.match(await page.locator('#wifi_mode_note').textContent(),/het toegewezen IP-adres van dit board/);
 assert.match(await page.locator('#help_wifi_mode').textContent(),/eerst zonder AP/);
 assert.match(await page.locator('#help_sta_delay_s').textContent(),/nog geen accesspoint/);
 assert.equal(await page.inputValue('#sta_delay_s'),'90');assert.equal(await page.inputValue('#sta_timeout_s'),'60');
 assert.equal(await page.locator('#sta_ssid').evaluate(e=>e.validity.valueMissing),true);
 await page.fill('#sta_ssid','Boat Router');await page.fill('#sta_password','router secret 123');
 assert.equal(await page.locator('#sta_password').evaluate(e=>e.checkValidity()),true);
 await page.fill('#sta_delay_s','0');await page.fill('#sta_timeout_s','10');
 assert.equal(await page.evaluate(()=>serialize().get('sta_delay_s')),'0');assert.equal(await page.evaluate(()=>serialize().get('sta_timeout_s')),'10');
 assert.equal(await page.evaluate(()=>serialize().get('sta_password')),'router secret 123');
 await page.fill('#sta_password','');assert.equal(await page.locator('#sta_password').evaluate(e=>e.checkValidity()),true);
 await page.evaluate(()=>{current.sta_password_set=false;visibility();});assert.equal(await page.locator('#sta_password').evaluate(e=>e.validity.valueMissing),true);
 await page.evaluate(()=>{current.sta_password_set=true;visibility();});assert.equal(await page.locator('#sta_password').evaluate(e=>e.checkValidity()),true);
 await page.fill('#sta_timeout_s','0');assert.equal(await page.locator('#sta_timeout_s').evaluate(e=>e.validity.rangeUnderflow),true);await page.fill('#sta_timeout_s','60');
 await page.selectOption('#wifi_mode','2');assert.equal(await page.locator('#field_sta_ssid').isVisible(),false);
 assert.match(await page.locator('#wifi_mode_note').textContent(),/WiFi blijft uit/);
 assert.equal(await page.evaluate(()=>serialize().get('sta_ssid')),'Boat Router');
 await page.selectOption('#wifi_mode','1');assert.equal(await page.inputValue('#sta_ssid'),'Boat Router');
 await page.evaluate(()=>applyFields(current));assert.equal(await page.evaluate(()=>formSignature()),originalForm);
 // Qualifying hold and post-qualification delay serialize independently and survive hiding.
 await page.click('#nav_io');
 assert.match(await page.locator('#rise_timing_note').textContent(),/Beide 0: direct na debounce/);
 for(const [id,value]of Object.entries({rise_hold_s:5,rise_delay_s:10,fall_hold_s:7,fall_delay_s:20}))await page.fill('#'+id,String(value));
 assert.match(await page.locator('#rise_timing_note').textContent(),/5 s \+ 10 s = 15 s onafgebroken AAN/);
 assert.match(await page.locator('#fall_timing_note').textContent(),/7 s \+ 20 s = 27 s onafgebroken UIT/);
 for(const [id,value]of Object.entries({rise_hold_s:5,rise_delay_s:10,fall_hold_s:7,fall_delay_s:20}))assert.equal(await page.evaluate(id=>serialize().get(id),id),String(value));
 await page.uncheck('#input_enabled');for(const id of ['rise_hold_s','rise_delay_s','fall_hold_s','fall_delay_s']){assert.equal(await page.locator('#field_'+id).isVisible(),false);assert.equal(await page.locator('#'+id).isDisabled(),true);}
 assert.equal(await page.evaluate(()=>serialize().get('rise_hold_s')),'5');assert.equal(await page.evaluate(()=>serialize().get('fall_delay_s')),'20');
 await page.check('#input_enabled');await page.fill('#rise_hold_s','3601');assert.equal(await page.locator('#rise_hold_s').evaluate(e=>e.validity.rangeOverflow),true);
 await page.evaluate(()=>applyFields(current));assert.equal(await page.evaluate(()=>formSignature()),originalForm);
 await page.click('#nav_status');
 assert.match(await page.locator('#test_info').textContent(),/zonder minimale AAN-\/UIT-tijd of actievertraging/);
 for(const [phase,value]of [['delay','Router opstarten'],['connecting','Router verbinden'],['connected','Connected'],['fallback','AP-terugval actief'],['off','WiFi uit']]){
  wifiSession={wifi_mode:phase==='off'?2:1,wifi_phase:phase,wifi_sta_connected:phase==='connected',wifi_sta_ssid:'Boat Router',wifi_sta_ip:phase==='connected'?'192.168.1.75':'',wifi_sta_rssi_dbm:phase==='connected'?-61:null,wifi_ap_active:phase==='fallback',wifi_ap_ip:phase==='fallback'?'192.168.4.1':'',wifi_clients:phase==='fallback'?1:0,wifi_next_s:['delay','connecting','fallback'].includes(phase)?20:0};
  await page.evaluate(()=>refreshStatus());assert.equal(await page.locator('#wifi_value').textContent(),value);
  if(phase==='connected'){assert.match(await page.locator('#wifi_detail').textContent(),/DHCP IP 192.168.1.75/);assert.match(await page.locator('#wifi_detail').textContent(),/RSSI -61 dBm/);assert.doesNotMatch(await page.locator('#wifi_detail').textContent(),/AP:/);}
  if(['delay','connecting'].includes(phase))assert.doesNotMatch(await page.locator('#wifi_detail').textContent(),/AP:/);
  if(phase==='fallback')assert.match(await page.locator('#wifi_detail').textContent(),/AP IP 192\.168\.4\.1/);
  if(phase==='connecting')assert.match(await page.locator('#wifi_detail').textContent(),/Poging nog/);
  if(phase==='off')assert.match(await page.locator('#wifi_detail').textContent(),/Geen AP-terugval/);
 }
 // A router link without a DHCP lease is not Connected, even if association succeeded.
 wifiSession={wifi_mode:1,wifi_phase:'connected',wifi_sta_connected:true,wifi_sta_ssid:'Boat Router',wifi_sta_ip:'',wifi_sta_rssi_dbm:-61,wifi_ap_active:false,wifi_ap_ip:'',wifi_clients:0};
 for(const ip of ['', '0.0.0.0']){wifiSession.wifi_sta_ip=ip;await page.evaluate(()=>refreshStatus());assert.equal(await page.locator('#wifi_value').textContent(),'Wacht op DHCP');assert.equal(await page.locator('#card_wifi').getAttribute('data-state'),'attention');assert.doesNotMatch(await page.locator('#wifi_detail').textContent(),/DHCP IP|AP:/);}
 // After DHCP success AP is off; the router-assigned address is not a static fallback.
 wifiSession={...wifiSession,wifi_sta_ip:'192.168.4.75'};await page.evaluate(()=>refreshStatus());
 assert.equal(await page.locator('#wifi_value').textContent(),'Connected');assert.match(await page.locator('#wifi_detail').textContent(),/DHCP IP 192\.168\.4\.75/);assert.doesNotMatch(await page.locator('#wifi_detail').textContent(),/AP:/);
 wifiSession={...wifiSession,wifi_phase:'fallback',wifi_sta_connected:false,wifi_sta_ip:'',wifi_ap_active:true,wifi_ap_ip:'192.168.4.1',wifi_clients:1};await page.evaluate(()=>refreshStatus());
 assert.equal(await page.locator('#wifi_value').textContent(),'AP-terugval actief');assert.match(await page.locator('#wifi_detail').textContent(),/AP IP 192\.168\.4\.1/);
 wifiSession={wifi_phase:'ap',wifi_ap_active:true,wifi_ap_ip:'192.168.4.1',wifi_clients:0};await page.evaluate(()=>refreshStatus());
 assert.equal(await page.locator('#wifi_value').textContent(),'Accesspoint actief');
 wifiSession.wifi_clients=1;await page.evaluate(()=>refreshStatus());assert.equal(await page.locator('#wifi_value').textContent(),'Connected');
 assert.equal(writes.length,0);assert.equal(await page.evaluate(()=>formSignature()),originalForm);
 await page.evaluate(()=>showStatus({...current,ble_result:7,ble_pending:0,ble_stage:16,ble_detail:13}));
 assert.equal(await page.locator('#card_ble').getAttribute('data-state'),'error');
 assert.match(await page.locator('#ble_value').textContent(),/mislukt/);
 assert.match(await page.locator('#ble_detail').textContent(),/Eerste register uitlezen \(13\)/);
 await page.evaluate(()=>showStatus({...current,ble_result:0,ble_pending:0}));
 assert.equal(await page.locator('#card_ble').getAttribute('data-state'),'neutral');
 await page.evaluate(()=>showStatus({...current,ble_result:12,ble_pending:1}));
 assert.equal(await page.locator('#card_ble').getAttribute('data-state'),'neutral');
 assert.match(await page.locator('#ble_value').textContent(),/bezig/);
 await page.evaluate(()=>showStatus({...current,ble_result:1,ble_pending:0}));
 assert.equal(await page.locator('#card_ble').getAttribute('data-state'),'ok');
 await page.click('#unit_wh');assert.match(await page.locator('#power_total').textContent(),/0.88 Wh/);assert.match(await page.locator('#power_total').textContent(),/6.42 Wh/);
 assert.equal(await page.locator('#power_q_wifi').textContent(),'0.225');
 const output=path.join(root,'test-results');fs.mkdirSync(output,{recursive:true});
 await page.screenshot({path:path.join(output,'status.png'),fullPage:true});
 await page.click('#nav_manage');
 await page.screenshot({path:path.join(output,'manage.png'),fullPage:true});
 await page.click('#nav_ble');assert.equal(await page.locator('#profile option').nth(1).getAttribute('value'),'3');
 assert.deepEqual(await page.locator('#profile option').evaluateAll(options=>options.map(o=>o.value)),['1','3','4','2']);
 assert.equal(await page.locator('#load_enabled').isEnabled(),true);assert.equal(await page.locator('#smp').isDisabled(),true);
 // Explicitly selecting MPPT after BatteryProtect must not retain its system
 // instance0; toggling other profiles must preserve a custom MPPT instance.
 await page.selectOption('#profile','1');assert.equal(await page.inputValue('#instance'),'3');
 await page.fill('#instance','5');await page.selectOption('#profile','4');
 assert.equal(await page.locator('#field_instance').isVisible(),false);
 assert.equal(await page.locator('#smp').isDisabled(),true);assert.equal(await page.inputValue('#smp'),'1');
 assert.match(await page.locator('#profile_note').textContent(),/48V-100A \(A3B3\)/);
 await page.selectOption('#profile','3');
 await page.selectOption('#profile','1');assert.equal(await page.inputValue('#instance'),'5');
 assert.equal(await page.locator('#instance_warning').count(),0);assert.equal(await page.locator('#instance_default').count(),0);
 await page.evaluate(()=>applyFields({...current,profile:1,instance:0}));
 assert.equal(await page.inputValue('#instance'),'0'); // Loading never silently saves a correction.
 assert.equal(await page.locator('#instance_warning').count(),0);
 await page.evaluate(()=>applyFields(current));
 assert.equal(await page.locator('#field_instance').isVisible(),false);assert.equal(await page.inputValue('#fn1_kind'),'11');
 assert.equal(await page.locator('#fn1_kind option[value="1"]').evaluate(e=>e.hidden),true);
 await page.selectOption('#profile','4');
 assert.equal(await page.inputValue('#fn1_kind'),'11');assert.equal(await page.inputValue('#fn2_kind'),'12');
 assert.equal(await page.locator('#fn1_kind option[value="11"]').evaluate(e=>e.hidden),false);
 for(const kind of ['1','3','4','5'])assert.equal(await page.locator('#fn1_kind option[value="'+kind+'"]').evaluate(e=>e.hidden),true);
 assert.equal(await page.evaluate(()=>serialize().get('profile')),'4');
 for(let i=3;i<=10;i++)await page.click('#add_function');
 assert.equal(await page.locator('#add_function').isDisabled(),true);assert.equal(await page.locator('#legend_fn10').isVisible(),true);
 await page.fill('#fn10_name','Router OFF');await page.selectOption('#fn10_kind','12');
 await page.click('#nav_lora');assert.equal(await page.locator('#field_dl_fn10').isVisible(),true);
 await page.check('#dl_fn10');assert.equal(await page.inputValue('#class'),'2');assert.match(await page.locator('#class_notice').textContent(),/netwerkserver/);
 await page.selectOption('#class','0'); // Explicit delayed Class A remains possible.
 await page.click('#nav_io');await page.selectOption('#rise_ble','10');
 const body=await page.evaluate(()=>Object.fromEntries(serialize()));
 assert.equal(body.function_count,'10');assert.equal(body.downlink_functions,'512');assert.equal(body.rising_fn,'10');assert.equal(body.schema,'10');assert.equal(body.fn10_kind,'12');
 assert.ok(new URLSearchParams(body).toString().length<7800);
 await page.selectOption('#io_board','0');assert.equal(await page.locator('#field_relay_pulse_ms').isVisible(),false);assert.equal(await page.locator('#legend_rising').isVisible(),false);
 await page.click('#nav_ble');await page.click('#remove_function');assert.equal(await page.locator('#legend_fn10').isVisible(),false);
 assert.equal(await page.evaluate(()=>serialize().get('downlink_functions')),'0');assert.equal(await page.evaluate(()=>serialize().get('fn10_kind')),'0');
 await page.selectOption('#profile','2');await page.selectOption('#fn1_kind','4');await page.fill('#fn1_service','12345678-0000-1000-8000-00805f9b34fb');await page.fill('#fn1_char','87654321-0000-1000-8000-00805f9b34fb');await page.fill('#fn1_value','0100');
 assert.equal(await page.locator('#field_fn1_service').isVisible(),true);assert.equal(await page.locator('#smp').isEnabled(),true);
 await page.click('#nav_lora');assert.equal(await page.locator('#field_join_eui').isVisible(),false);
 await page.fill('#net0_name','');await page.locator('#net0_name').pressSequentially('Milesight UG63 / UG65');assert.equal(await page.inputValue('#net0_name'),'Milesight UG63 / UG65');assert.equal(await page.locator('#net0_name').evaluate(e=>document.activeElement===e),true);
 await page.click('#net_up_1');assert.deepEqual(await page.evaluate(()=>networkOrder),[1,0,2,3]);
 let networkBody=await page.evaluate(()=>Object.fromEntries(serialize()));assert.equal(networkBody.net0_order,'1');assert.equal(networkBody.net1_order,'0');assert.equal(networkBody.net0_app_key,'');
 await page.locator('#network_summary_1').click();await page.fill('#net1_app_key','00112233445566778899aabbccddeeff');
 await page.click('#net_down_1');networkBody=await page.evaluate(()=>Object.fromEntries(serialize()));assert.equal(networkBody.net1_app_key,'00112233445566778899aabbccddeeff');assert.equal(networkBody.net0_app_key,'');
 await page.evaluate(()=>{for(let i=0;i<4;i++)$('network_details_'+i).open=false;});
 await page.locator('#net_drag_1').dragTo(page.locator('#legend_net0'));assert.deepEqual(await page.evaluate(()=>networkOrder),[1,0,2,3]);
 await page.evaluate(()=>showStatus({...current,network_slot:0,network_state:1,joined:0,network_preempt_s:900,network_retry_s:120}));
 assert.match(await page.locator('#lora_detail').textContent(),/Voorkeur opnieuw/);
 await page.screenshot({path:path.join(output,'networks.png'),fullPage:true});
 // Exercise the largest supported form including percent-expanded names.
 await page.evaluate(()=>{for(let i=1;i<=10;i++){for(const suffix of ['service','char'])$('fn'+i+'_'+suffix).value='12345678-1234-1234-1234-123456789abc';$('fn'+i+'_value').value='ff'.repeat(20);$('fn'+i+'_name').value='&'.repeat(24);}for(let i=0;i<4;i++){$('net'+i+'_name').value='&'.repeat(24);$('net'+i+'_app_key').value='ff'.repeat(16);} $('wifi_mode').value='1';$('wifi_ssid').value='&'.repeat(32);$('wifi_password').value='&'.repeat(63);$('sta_ssid').value='&'.repeat(32);$('sta_password').value='&'.repeat(63);for(const edge of ['rise','fall'])for(const kind of ['hold_s','delay_s'])$(edge+'_'+kind).value='3600';visibility();});
 const largestBody=(await page.evaluate(()=>serialize().toString())).length;assert.ok(largestBody<maxBody,'Largest form '+largestBody+' exceeds '+maxBody);
 await page.evaluate(()=>{for(const f of F)if(f[3]==='password')$(f[0]).value='';applyFields(current);});
 await page.click('#lang_en');assert.equal(await page.locator('#add_function').textContent(),'+ Add function');
 await page.setViewportSize({width:390,height:844});
 for(const id of ['status','ble','io','lora','wifi','manage']){await page.click('#nav_'+id);assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true,id+' overflows');}
 await page.click('#nav_status');await page.screenshot({path:path.join(output,'status-mobile.png'),fullPage:true});
 await page.click('#nav_lora');await page.locator('#network_summary_1').click();assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);await page.screenshot({path:path.join(output,'networks-mobile.png'),fullPage:true});
 assert.deepEqual(errors,[]);assert.equal(saved,undefined);
 // Log refresh must not replace a queued LoRa test with a Bluetooth scan result.
 await page.click('#nav_status');
 const beforeLoraActions=writes.length;
 let loraDialogs=0;const trackLoraDialog=()=>loraDialogs++;page.on('dialog',trackLoraDialog);
 await page.click('#test_uplink');await page.waitForFunction(()=>!saving);
 assert.equal(writes.at(-1).route,'/action');assert.equal(saved.get('command'),'uplink');
 assert.match(await page.locator('#test_result').textContent(),/Only the request is queued/);
 const uplinkResult=await page.locator('#test_result').textContent();
 logEntries=[[1,10,1],[2,30,7],[3,31,8],[4,32,4294967290],[5,33,7],[6,34,9],[7,35,2],[8,36,4294967293]];
 await page.evaluate(()=>refreshLog());assert.equal(await page.locator('#test_result').textContent(),uplinkResult);
 assert.match(await page.locator('#scan_result').textContent(),/Last Bluetooth scan/);
 const logText=await page.locator('#log_rows').textContent();
 for(const text of ['Local LoRa TX complete (not proof of ACK)','accepted by radio','rejected by radio','MAC -6','network ACK received','No LoRa network ACK','Profile 2','MAC -3'])assert.ok(logText.includes(text),text);
 await page.click('#test_join_now');await page.waitForFunction(()=>!saving);assert.equal(saved.get('command'),'join_now');
 assert.match(await page.locator('#test_result').textContent(),/only the queue, not a successful network join/);
 allowAction=false;await page.click('#test_uplink');await page.waitForFunction(()=>!saving);assert.match(await page.locator('#test_result').textContent(),/LoRa request not confirmed/);allowAction=true;
 assert.equal(loraDialogs,0);page.off('dialog',trackLoraDialog);
 assert.equal(writes.length,beforeLoraActions+3);assert.equal(writes.some(w=>w.route==='/save'),false);
 // Reset only the isolated mock's command history for the save-workflow checks.
 writes.length=0;saved=undefined;logEntries=[[0,1,0],[3,9,1]];
 // Reorder is a draft. It survives status refresh and tab changes without writes.
 await page.reload();await page.waitForFunction(()=>ready);await page.click('#nav_lora');
 assert.equal(await page.locator('#save').isDisabled(),true);
 await page.click('#net_up_1');await page.evaluate(()=>refreshStatus());
 assert.match(await page.locator('#save_note').textContent(),/Niet opgeslagen/);
 assert.equal(await page.locator('#save').isEnabled(),true);assert.equal(writes.length,0);
 await page.click('#nav_wifi');await page.click('#nav_lora');
 assert.deepEqual(await page.evaluate(()=>networkOrder),[1,0,2,3]);assert.equal(writes.length,0);
 await page.click('#net_down_1');assert.equal(await page.locator('#save').isDisabled(),true);
 await page.evaluate(()=>{for(let i=0;i<4;i++)$('network_details_'+i).open=false;});
 await page.locator('#net_drag_1').dragTo(page.locator('#legend_net0'));assert.deepEqual(await page.evaluate(()=>networkOrder),[1,0,2,3]);assert.equal(await page.locator('#save').isEnabled(),true);
 assert.equal(writes.length,0);
 // Saving exposes the first invalid field even in another tab/closed details.
 await page.evaluate(()=>{$('net1_join_eui').value='bad';$('network_details_1').open=false;});
 await page.click('#nav_wifi');await page.click('#save');
 assert.equal(await page.locator('#nav_lora').getAttribute('aria-current'),'page');
 assert.equal(await page.locator('#network_details_1').evaluate(e=>e.open),true);
 assert.match(await page.locator('#result').textContent(),/JoinEUI/);assert.equal(writes.length,0);
 await page.fill('#net1_join_eui',config.net1_join_eui);
 saveCode='invalid_settings';await page.click('#save');await page.waitForFunction(()=>!saving);
 assert.equal(writes.length,1);assert.equal(saved.get('net0_order'),'1');assert.equal(saved.get('net1_order'),'0');
 assert.equal(await page.locator('#save').isEnabled(),true);
 saveCode='unchanged';await page.click('#save');await page.waitForFunction(()=>!saving);
 assert.equal(await page.locator('#save').isDisabled(),true);assert.match(await page.locator('#result').textContent(),/al opgeslagen/);
 // A successful save reconnects instead of leaving the page permanently locked.
 await page.click('#net_down_1');
 const changedSettings={profile:4,tx_dbm:19,wifi_mode:1,sta_ssid:'Boat Router',sta_delay_s:0,sta_timeout_s:45,rise_hold_s:5,rise_delay_s:10,fall_hold_s:7,fall_delay_s:20};
 await page.evaluate(settings=>{for(const [key,value]of Object.entries(settings))$(key).value=String(value);$('sta_password').value='new router password';visibility();updateDirty();},changedSettings);
 saveCode='saved';await page.click('#save');
 await page.waitForFunction(()=>ready&&!saving,{},{timeout:15000});assert.equal(config.revision,8);
 for(const [key,value]of Object.entries(changedSettings)){assert.equal(config[key],value);assert.equal(await page.inputValue('#'+key),String(value));}
 assert.equal(storedStaPassword,'new router password');assert.equal(await page.inputValue('#sta_password'),'');
 assert.equal(await page.locator('#save').isDisabled(),true);
 await page.click('#nav_manage');assert.equal(await page.locator('#apply_import').isDisabled(),true);assert.equal(await page.locator('#ota_upload').isDisabled(),true);
 await page.screenshot({path:path.join(output,'manage-mobile-nl.png'),fullPage:true});
 // Backup uses saved values, never draft edits or passwords.
 await page.evaluate(()=>{$('wifi_ssid').value='Draft SSID';$('wifi_password').value='DraftPassword123';$('sta_password').value='DraftRouterSecret123';$('tx_dbm').value='2';$('rise_hold_s').value='99';updateDirty();});
 const downloadPromise=page.waitForEvent('download');await page.click('#export');const download=await downloadPromise;
 const backup=JSON.parse(fs.readFileSync(await download.path(),'utf8'));
 assert.equal(backup.settings.wifi_ssid,config.wifi_ssid);
 assert.equal(backup.schema,10);for(const [key,value]of Object.entries(changedSettings))assert.equal(backup.settings[key],value);
 assert.ok(!JSON.stringify(backup).includes('DraftPassword'));
 assert.ok(!JSON.stringify(backup).includes('DraftRouterSecret'));assert.ok(!JSON.stringify(backup).includes(storedStaPassword));
 for(const key of ['wifi_password','sta_password','victron_pin','app_key','dev_eui','net0_app_key'])assert.ok(!(key in backup.settings));
 let count=writes.length;
 await page.setInputFiles('#import',{name:'wrong.json',mimeType:'application/json',buffer:Buffer.from('{}')});
 await page.waitForFunction(()=>$('import_result').textContent.includes('Ongeldige'));assert.equal(await page.locator('#apply_import').isDisabled(),true);assert.equal(writes.length,count);
 // Legacy backups receive explicit compatibility defaults, not current draft values.
 for(const schema of [8,9]){
  const legacy=JSON.parse(JSON.stringify(backup));legacy.schema=schema;
  for(const key of Object.keys(newDefaults))delete legacy.settings[key];
  if(schema===8)for(const key of Object.keys(legacy.settings))if(key.startsWith('net'))delete legacy.settings[key];
  await page.setInputFiles('#import',{name:'legacy-'+schema+'.json',mimeType:'application/json',buffer:Buffer.from(JSON.stringify(legacy))});
  await page.waitForFunction(()=>!!pendingImport);for(const [key,value]of Object.entries(newDefaults))assert.equal(await page.evaluate(key=>pendingImport[key],key),value);
  assert.equal(await page.evaluate(()=>pendingImport.net1_name),config.net1_name);assert.equal(writes.length,count);
 }
 const badCeiling=JSON.parse(JSON.stringify(backup));badCeiling.settings.tx_dbm=23;
 await page.setInputFiles('#import',{name:'invalid-ceiling.json',mimeType:'application/json',buffer:Buffer.from(JSON.stringify(badCeiling))});
 await page.waitForFunction(()=>$('import_result').textContent.includes('Ongeldige'));assert.equal(await page.locator('#apply_import').isDisabled(),true);
 backup.settings.wifi_ssid='Restored LoRaBLE';
 await page.setInputFiles('#import',{name:'backup.json',mimeType:'application/json',buffer:Buffer.from(JSON.stringify(backup))});
 await page.waitForFunction(()=>!$('apply_import').disabled);assert.equal(writes.length,count);
 acceptDialog=false;await page.click('#apply_import');assert.equal(writes.length,count);assert.equal(await page.inputValue('#wifi_ssid'),'Draft SSID');
 acceptDialog=true;await page.click('#apply_import');await page.waitForFunction(()=>ready&&!saving,{},{timeout:15000});
 assert.equal(writes.length,count+1);assert.equal(saved.get('wifi_ssid'),'Restored LoRaBLE');assert.equal(saved.get('wifi_password'),'');
 assert.equal(saved.get('sta_password'),'');assert.equal(storedStaPassword,'new router password');
 for(const [key,value]of Object.entries(changedSettings))assert.equal(config[key],value);
 assert.equal(config.wifi_ssid,'Restored LoRaBLE');assert.equal(await page.locator('#save').isDisabled(),true);
 // Reboot has its own explicit, authenticated action, never a save/factory reset.
 count=writes.length;acceptDialog=false;await page.click('#reboot');assert.equal(writes.length,count);
 acceptDialog=true;allowAction=false;await page.click('#reboot');await page.waitForFunction(()=>!saving);
 assert.match(await page.locator('#reboot_result').textContent(),/niet bevestigd/);assert.equal(await page.locator('#reboot').isEnabled(),true);
 allowAction=true;const revision=config.revision;await page.click('#reboot');await page.waitForFunction(()=>ready&&!saving,{},{timeout:15000});
 assert.equal(writes.at(-1).route,'/action');assert.equal(new URLSearchParams(writes.at(-1).body.toString()).get('command'),'reboot');assert.equal(config.revision,revision);
 // File selection never uploads. Invalid, cancelled and rejected uploads stay usable.
 count=writes.length;
 await page.setInputFiles('#ota_file',{name:'wrong.bin',mimeType:'application/octet-stream',buffer:Buffer.alloc(256)});await page.click('#ota_upload');
 await page.waitForFunction(()=>$('ota_result').textContent.includes('geen compleet'));assert.equal(writes.length,count);
 const bundle=Buffer.alloc(256);bundle.write('LBRUPD1\0');bundle.write('4.12.1',16);
 await page.setInputFiles('#ota_file',{name:'LoRaBLE-Remote-4.12.1.bin',mimeType:'application/octet-stream',buffer:bundle});assert.equal(writes.length,count);
 acceptDialog=false;const cancelUpdate=page.waitForEvent('dialog');await page.click('#ota_upload');await cancelUpdate;await page.waitForFunction(()=>!saving);assert.equal(writes.length,count);
 acceptDialog=true;const uploadReply=page.waitForResponse(r=>r.url().endsWith('/ota'));await page.click('#ota_upload');await uploadReply;await page.waitForFunction(()=>!saving);
 assert.equal(writes.at(-1).route,'/ota');assert.ok(writes.at(-1).body.equals(bundle));assert.equal(await page.locator('#ota_upload').isEnabled(),true);
 await page.setInputFiles('#ota_file',[]);await page.setInputFiles('#import',[]);
 assert.equal(await page.locator('#ota_upload').isDisabled(),true);assert.equal(await page.locator('#apply_import').isDisabled(),true);
 await page.click('#lang_en');assert.equal(await page.locator('#import_browse').textContent(),'Browse…');assert.equal(await page.locator('#ota_browse').textContent(),'Browse…');assert.equal(await page.locator('#reboot').textContent(),'Restart');
 for(const width of [390,1100]){await page.setViewportSize({width,height:950});assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);await page.screenshot({path:path.join(output,'manage-'+width+'-en.png'),fullPage:true});}
 // New settings render without overflow in both languages and viewport sizes.
 for(const language of ['nl','en']){
  await page.click('#lang_'+language);
  for(const width of [390,1100]){await page.setViewportSize({width,height:950});for(const tab of ['ble','wifi','io','lora']){await page.click('#nav_'+tab);assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);await page.screenshot({path:path.join(output,tab+'-settings-'+language+'-'+width+'.png'),fullPage:true});}}
 }
 // Explicit WiFi off saves once and gives USB guidance rather than polling an AP.
 await page.click('#nav_wifi');await page.selectOption('#wifi_mode','2');count=writes.length;await page.click('#save');await page.waitForFunction(()=>!saving&&!ready);
 assert.equal(writes.length,count+1);assert.equal(saved.get('wifi_mode'),'2');assert.match(await page.locator('#result').textContent(),/WiFi off, without AP fallback/);
 assert.deepEqual(errors,[]);
 console.log('PASS: explicit draft/save, validation focus, unchanged/reconnect handling, private backup, confirmed restore/reboot/update, cancellation, rejection, no implicit writes, desktop/mobile management.');
 console.log('PASS: four networks, priority buttons and drag, stable key slots, preempt status, largest form, ten functions, profile guards, Class C, power units, NL/EN and mobile layout.');
 console.log('PASS: LoRa queued/accepted/local TX/ACK separation, failed joins, signed RSSI/SNR and MAC codes, old other-profile RX, configurable power ceiling with independent ADR, manual join and confirmed test actions.');
 console.log('PASS: DHCP-first router/AP/off modes, no initial AP, fallback after loss/failure, DHCP-required Connected, startup/attempt-window bounds, phase/IP/RSSI display, independent edge timing, saved range/timing/router values, write-only secrets, schema 8/9 migration, schema 10 backup, NL/EN settings layouts; largest form '+largestBody+'/'+maxBody+' bytes.');
 }finally{if(browser)await browser.close();server.close();}
})().catch(e=>{console.error(e);process.exitCode=1;server.close();});
