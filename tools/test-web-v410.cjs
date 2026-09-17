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
for(let i=1;i<=10;i++)Object.assign(config,{['fn'+i+'_name']:'Function '+i,['fn'+i+'_kind']:i<3?10+i:0,['fn'+i+'_service']:'',['fn'+i+'_char']:'',['fn'+i+'_value']:''});
config.network_health_min=240;config.network_slot=0;config.network_state=3;
for(let i=0;i<4;i++)Object.assign(config,{['net'+i+'_name']:['Private UG65','TTN','Network 3','Network 4'][i],['net'+i+'_enabled']:i<2?1:0,['net'+i+'_kind']:i===1?1:0,['net'+i+'_key_set']:i<2?1:0,['net'+i+'_join_eui']:i?'000000000000000'+i:'0123456789ABCDEF',['net'+i+'_rx2_custom']:0,['net'+i+'_rx2_freq']:869525000,['net'+i+'_rx2_dr']:0,['net'+i+'_preempt_min']:1440,['net'+i+'_order']:i});
let saved;
const server=http.createServer((req,res)=>{
 res.setHeader('Content-Type','application/json');
 if(req.url==='/session')res.end(JSON.stringify({token:'test-token',native:true,esp_firmware:'4.10.0',wifi_clients:1,wifi_rssi_dbm:[-42],ble_target_mac:config.victron_mac,ble_target_state:1,ble_target_age_s:5}));
 else if(req.url==='/config')res.end(JSON.stringify(config));
 else if(req.url==='/status')res.end(JSON.stringify({...config,firmware:'4.10.0',uptime_s:4567,tx_count:3}));
 else if(req.url==='/log')res.end('{"entries":[[0,1,0],[3,9,1]]}');
 else if(req.url==='/save'||req.url==='/action'){assert.equal(req.headers['x-lorable'],'test-token');let body='';req.on('data',d=>body+=d);req.on('end',()=>{saved=new URLSearchParams(body);res.end('{"ok":true,"code":"saved"}');});}
 else{res.setHeader('Content-Type','text/html');res.end(html);}
});
(async()=>{
 await new Promise(r=>server.listen(0,'127.0.0.1',r));let browser;
 try{
 browser=await chromium.launch({executablePath:'C:/Program Files/Google/Chrome/Application/chrome.exe',headless:true});
 const page=await browser.newPage({viewport:{width:1100,height:950}}),errors=[];page.on('pageerror',e=>errors.push(e.message));page.on('dialog',d=>d.accept());
 await page.goto('http://127.0.0.1:'+server.address().port);await page.waitForFunction(()=>typeof ready!=='undefined'&&ready);
 await page.waitForFunction(()=>document.getElementById('wifi_value').textContent==='Connected');
 assert.match(await page.locator('#power_total').textContent(),/267 mAh/);assert.match(await page.locator('#power_total').textContent(),/1947 mAh/);
 await page.click('#unit_wh');assert.match(await page.locator('#power_total').textContent(),/0.88 Wh/);assert.match(await page.locator('#power_total').textContent(),/6.42 Wh/);
 assert.equal(await page.locator('#power_q_wifi').textContent(),'0.225');
 const output=path.join(root,'test-results');fs.mkdirSync(output,{recursive:true});
 await page.screenshot({path:path.join(output,'portal-v410-status.png'),fullPage:true});
 await page.click('#nav_ble');assert.equal(await page.locator('#profile option').nth(1).getAttribute('value'),'3');
 assert.equal(await page.locator('#load_enabled').isEnabled(),true);assert.equal(await page.locator('#smp').isDisabled(),true);
 assert.equal(await page.locator('#field_instance').isVisible(),false);assert.equal(await page.inputValue('#fn1_kind'),'11');
 assert.equal(await page.locator('#fn1_kind option[value="1"]').evaluate(e=>e.hidden),true);
 for(let i=3;i<=10;i++)await page.click('#add_function');
 assert.equal(await page.locator('#add_function').isDisabled(),true);assert.equal(await page.locator('#legend_fn10').isVisible(),true);
 await page.fill('#fn10_name','Router OFF');await page.selectOption('#fn10_kind','12');
 await page.click('#nav_lora');assert.equal(await page.locator('#field_dl_fn10').isVisible(),true);
 await page.check('#dl_fn10');assert.equal(await page.inputValue('#class'),'2');assert.match(await page.locator('#class_notice').textContent(),/netwerkserver/);
 await page.selectOption('#class','0'); // Explicit delayed Class A remains possible.
 await page.click('#nav_io');await page.selectOption('#rise_ble','10');
 const body=await page.evaluate(()=>Object.fromEntries(serialize()));
 assert.equal(body.function_count,'10');assert.equal(body.downlink_functions,'512');assert.equal(body.rising_fn,'10');assert.equal(body.schema,'9');assert.equal(body.fn10_kind,'12');
 assert.ok(new URLSearchParams(body).toString().length<7800);
 await page.selectOption('#io_board','0');assert.equal(await page.locator('#field_relay_pulse_ms').isVisible(),false);assert.equal(await page.locator('#legend_rising').isVisible(),false);
 await page.click('#nav_ble');await page.click('#remove_function');assert.equal(await page.locator('#legend_fn10').isVisible(),false);
 assert.equal(await page.evaluate(()=>serialize().get('downlink_functions')),'0');assert.equal(await page.evaluate(()=>serialize().get('fn10_kind')),'0');
 await page.selectOption('#profile','2');await page.selectOption('#fn1_kind','4');await page.fill('#fn1_service','12345678-0000-1000-8000-00805f9b34fb');await page.fill('#fn1_char','87654321-0000-1000-8000-00805f9b34fb');await page.fill('#fn1_value','0100');
 assert.equal(await page.locator('#field_fn1_service').isVisible(),true);assert.equal(await page.locator('#smp').isEnabled(),true);
 await page.click('#nav_lora');assert.equal(await page.locator('#field_join_eui').isVisible(),false);
 await page.fill('#net0_name','');await page.locator('#net0_name').pressSequentially('Private UG65');assert.equal(await page.inputValue('#net0_name'),'Private UG65');assert.equal(await page.locator('#net0_name').evaluate(e=>document.activeElement===e),true);
 await page.click('#net_up_1');assert.deepEqual(await page.evaluate(()=>networkOrder),[1,0,2,3]);
 let networkBody=await page.evaluate(()=>Object.fromEntries(serialize()));assert.equal(networkBody.net0_order,'1');assert.equal(networkBody.net1_order,'0');assert.equal(networkBody.net0_app_key,'');
 await page.locator('#network_summary_1').click();await page.fill('#net1_app_key','00112233445566778899aabbccddeeff');
 await page.click('#net_down_1');networkBody=await page.evaluate(()=>Object.fromEntries(serialize()));assert.equal(networkBody.net1_app_key,'00112233445566778899aabbccddeeff');assert.equal(networkBody.net0_app_key,'');
 await page.evaluate(()=>{for(let i=0;i<4;i++)$('network_details_'+i).open=false;});
 await page.locator('#net_drag_1').dragTo(page.locator('#legend_net0'));assert.deepEqual(await page.evaluate(()=>networkOrder),[1,0,2,3]);
 await page.evaluate(()=>showStatus({...current,network_slot:0,network_state:1,joined:0,network_preempt_s:900,network_retry_s:120}));
 assert.match(await page.locator('#lora_detail').textContent(),/Voorkeur opnieuw/);
 await page.screenshot({path:path.join(output,'portal-v410-networks.png'),fullPage:true});
 // Exercise the largest supported form including percent-expanded names.
 await page.evaluate(()=>{for(let i=1;i<=10;i++){for(const suffix of ['service','char'])$('fn'+i+'_'+suffix).value='12345678-1234-1234-1234-123456789abc';$('fn'+i+'_value').value='ff'.repeat(20);$('fn'+i+'_name').value='&'.repeat(24);}for(let i=0;i<4;i++)$('net'+i+'_name').value='&'.repeat(24);});
 assert.ok((await page.evaluate(()=>serialize().toString())).length<maxBody);
 await page.evaluate(()=>applyFields(current));
 await page.click('#lang_en');assert.equal(await page.locator('#add_function').textContent(),'+ Add function');
 await page.setViewportSize({width:390,height:844});
 for(const id of ['status','ble','io','lora','wifi','manage']){await page.click('#nav_'+id);assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true,id+' overflows');}
 await page.click('#nav_status');await page.screenshot({path:path.join(output,'portal-v410-mobile.png'),fullPage:true});
 await page.click('#nav_lora');await page.locator('#network_summary_1').click();assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);await page.screenshot({path:path.join(output,'portal-v410-networks-mobile.png'),fullPage:true});
 assert.deepEqual(errors,[]);assert.equal(saved,undefined);
 console.log('PASS: four networks, priority buttons and drag, stable key slots, preempt status, largest form, ten functions, profile guards, Class C, power units, NL/EN and mobile layout.');
 }finally{if(browser)await browser.close();server.close();}
})().catch(e=>{console.error(e);process.exitCode=1;server.close();});
