// Isolated mock portal; never sends commands to physical hardware.
const fs=require('node:fs'),path=require('node:path'),http=require('node:http'),assert=require('node:assert/strict');
const {chromium}=require(process.env.PLAYWRIGHT_PATH||'playwright');
const root=path.resolve(__dirname,'..'),html=fs.readFileSync(path.join(root,'web/index.html'));
const config={language:0,wifi_ssid:'Victron LoRaBLE Remote',profile:3,load_enabled:1,victron_mac:'AA:BB:CC:DD:EE:FF',address_type:-1,instance:0,smp:1,window_min:60,status_min:15,ble_attempts:3,region:4,class:0,fport:10,subband:0,adr:1,dev_eui:'0123456789ABCDEF',join_eui:'0123456789ABCDEF',rx2_custom:0,rx2_freq:869525000,rx2_dr:0,ble_available:1,joined:1,ble_result:1,revision:7,input_enabled:1,relay_enabled:1,rising_actions:5,falling_actions:1,relay_pulse_ms:1000,io_board:1,wifi_triggers:3,wifi_input_min:60,wifi_lora_min:60,downlink_allowed:28,function_count:2,rising_fn:1,falling_fn:0,downlink_functions:0};
for(let i=1;i<=10;i++)Object.assign(config,{['fn'+i+'_name']:'Function '+i,['fn'+i+'_kind']:i<3?10+i:0,['fn'+i+'_service']:'',['fn'+i+'_char']:'',['fn'+i+'_value']:''});
let saved;
const server=http.createServer((req,res)=>{
 res.setHeader('Content-Type','application/json');
 if(req.url==='/session')res.end(JSON.stringify({token:'test-token',native:true,esp_firmware:'4.9.0-dev',wifi_clients:1,wifi_rssi_dbm:[-42]}));
 else if(req.url==='/config')res.end(JSON.stringify(config));
 else if(req.url==='/status')res.end(JSON.stringify({...config,firmware:'4.9.0',uptime_s:4567,tx_count:3}));
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
 await page.screenshot({path:path.join(output,'portal-v49-status.png'),fullPage:true});
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
 assert.equal(body.function_count,'10');assert.equal(body.downlink_functions,'512');assert.equal(body.rising_fn,'10');assert.equal(body.schema,'8');assert.equal(body.fn10_kind,'12');
 assert.ok(new URLSearchParams(body).toString().length<5800);
 await page.selectOption('#io_board','0');assert.equal(await page.locator('#field_relay_pulse_ms').isVisible(),false);assert.equal(await page.locator('#legend_rising').isVisible(),false);
 await page.click('#nav_ble');await page.click('#remove_function');assert.equal(await page.locator('#legend_fn10').isVisible(),false);
 assert.equal(await page.evaluate(()=>serialize().get('downlink_functions')),'0');assert.equal(await page.evaluate(()=>serialize().get('fn10_kind')),'0');
 await page.selectOption('#profile','2');await page.selectOption('#fn1_kind','4');await page.fill('#fn1_service','12345678-0000-1000-8000-00805f9b34fb');await page.fill('#fn1_char','87654321-0000-1000-8000-00805f9b34fb');await page.fill('#fn1_value','0100');
 assert.equal(await page.locator('#field_fn1_service').isVisible(),true);assert.equal(await page.locator('#smp').isEnabled(),true);
 await page.click('#lang_en');assert.equal(await page.locator('#add_function').textContent(),'+ Add function');
 await page.setViewportSize({width:390,height:844});
 for(const id of ['status','ble','io','lora','wifi','manage']){await page.click('#nav_'+id);assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true,id+' overflows');}
 await page.click('#nav_status');await page.screenshot({path:path.join(output,'portal-v49-mobile.png'),fullPage:true});
 assert.deepEqual(errors,[]);assert.equal(saved,undefined);
 console.log('PASS: ten functions, stable routes, BatteryProtect profile, Class C notice, mAh/Wh arithmetic, NL/EN and mobile layout.');
 }finally{if(browser)await browser.close();server.close();}
})().catch(e=>{console.error(e);process.exitCode=1;server.close();});
