// Allowlisted packaging only. Never copy the workspace wholesale.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {fileURLToPath} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const out=path.resolve(process.argv[2]||path.join(root,'release_staging/4.10.0'));
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
assert.ok(out!==root&&!root.startsWith(out+path.sep),'Choose a separate staging subdirectory, not the workspace or its parent.');
assert.ok(!fs.existsSync(out)||fs.readdirSync(out).length===0,'Destination must be new/empty. Nothing is removed or overwritten.');
const stm=path.join(root,'build_public410'),esp=path.join(root,'esp8684/build_native410');
const options=JSON.parse(fs.readFileSync(path.join(stm,'build.options.json')));
assert.match(options.customBuildProperties,/-DLORABLE_PUBLIC_BUILD/);
const project=JSON.parse(fs.readFileSync(path.join(esp,'project_description.json')));
assert.equal(project.project_version,'4.10.0');assert.equal(project.target,'esp32c2');
const local=fs.existsSync(path.join(root,'stm32/settings.local.h'))?fs.readFileSync(path.join(root,'stm32/settings.local.h'),'utf8'):'';
const privateValues=[];
for(const m of local.matchAll(/^\s*#define\s+(LORAWAN_(?:APPKEY|APPEUI|DEVEUI)|WIFI_AP_PASSWORD|VICTRON_MAC)\s+([^\r\n]+)/gm)){
 const str=m[2].match(/"([^"]+)"/);if(str&&str[1].length>=8)privateValues.push(Buffer.from(str[1]));
 const pairs=[...m[2].matchAll(/0x([0-9a-f]{2})\b/gi)].map(x=>parseInt(x[1],16));
 if(pairs.length>=8&&pairs.some(x=>x)){
  const raw=Buffer.from(pairs);privateValues.push(raw,Buffer.from(raw.toString('hex')),Buffer.from(raw.toString('hex').toUpperCase()));
 }
}
function scan(buffer,label){for(const value of privateValues)assert.equal(buffer.includes(value),false,'Private installation value found in '+label);}
const files=new Map();
function add(rel,source=path.join(root,rel)){const b=fs.readFileSync(source);scan(b,rel);files.set(rel.replaceAll('\\','/'),b);}
function flat(dir,test){for(const e of fs.readdirSync(path.join(root,dir),{withFileTypes:true}))if(e.isFile()&&test(e.name))add(dir+'/'+e.name);}
for(const f of ['README.md','README.en.md','LICENSE','.gitignore','.gitattributes','THIRD-PARTY-NOTICES.md','POWER-BUDGET.md','BATTERYPROTECT-RESEARCH.md','VALIDATION-v49.md','VALIDATION-v410.md'])add(f);
flat('stm32',n=>/\.(h|cpp|ino|js)$/.test(n)&&n!=='settings.local.h');
flat('esp8684/main',n=>/\.(c|h|txt)$/.test(n));
for(const f of ['esp8684/CMakeLists.txt','esp8684/partitions.csv','esp8684/sdkconfig.defaults','esp8684/build.ps1','web/index.html','arduino/platform.local.txt'])add(f);
flat('docs',n=>n.endsWith('.md'));flat('installer',n=>/\.(cmd|ps1)$/.test(n));
flat('.github/workflows',n=>n.endsWith('.yml'));
for(const f of ['build.ps1','build-web.mjs','check-web-asset.mjs','pack-esp-ota.py','test-ota-pack.py','test-codec.cjs','test-web-v410.cjs','test-batteryprotect-protocol.py','probe_batteryprotect_control.py','probe_ble_readonly.py','serve-esp-ota.py','prepare-release.mjs','make-release-zip.py'])add('tools/'+f);
flat('tools/tests',n=>/\.(h|cpp)$/.test(n));
flat('tools/tests/manager',n=>n.endsWith('.h'));
for(const name of ['portal-v410-status.png','portal-v410-mobile.png','portal-v410-networks.png','portal-v410-networks-mobile.png'])if(fs.existsSync(path.join(root,'test-results',name)))add('docs/images/'+name,path.join(root,'test-results',name));
add('firmware/LoRaBLE-STM32-4.10.0.bin',path.join(stm,'stm32.ino.bin'));
add('firmware/LoRaBLE-ESP8684-4.10.0.packed',path.join(esp,'LoRaBLE-ESP8684-4.10.0.packed'));
// Scan the uncompressed companion too; private strings must not hide in XZ.
scan(fs.readFileSync(path.join(esp,'lorable_esp8684.bin')),'ESP application');
// Original license texts, with relative provenance paths; no SDK code/binaries.
function licenses(base,label){
 assert.ok(fs.existsSync(base),'SDK notice source missing: '+label);
 function visit(dir){for(const e of fs.readdirSync(dir,{withFileTypes:true})){
  if(e.isSymbolicLink()||['.git','build','node_modules','__pycache__'].includes(e.name))continue;
  const source=path.join(dir,e.name);
  if(e.isDirectory())visit(source);
  else if(/^(licen[sc]e|copying|notice|copyright|exception)([._-].*)?$/i.test(e.name)||/^license.*\.txt$/i.test(e.name)){
   if(fs.statSync(source).size>200000)continue;
   add('third_party_notices/'+label+'/'+path.relative(base,source),source);
  }
 }}visit(base);
}
const user=process.env.USERPROFILE;
licenses(process.env.LORABLE_RUI_ROOT||path.join(process.env.LOCALAPPDATA,'Arduino15/packages/rak_rui/hardware/stm32/4.2.4'),'RAK-RUI-4.2.4');
licenses(process.env.IDF_PATH||path.join(user,'.cache/esp-idf-v5.5.5'),'ESP-IDF-5.5.5');
// Preserve runtime exceptions from the compiler distributions when available.
for(const [base,label]of [[path.join(process.env.LOCALAPPDATA,'Arduino15/packages/rak_rui/tools/arm-none-eabi-gcc'),'ARM-toolchain'],[path.join(user,'.cache/espressif-v5.5.5-tools/tools/riscv32-esp-elf'),'RISC-V-toolchain']])if(fs.existsSync(base))licenses(base,label);
const images=[...files].filter(([p])=>p.startsWith('firmware/')).map(([p,b])=>({path:p,target:p.endsWith('.bin')?'stm32-usb':'esp8684-browser',bytes:b.length,sha256:sha(b)}));
files.set('firmware/manifest.json',Buffer.from(JSON.stringify({version:'4.10.0',release_status:'release',images},null,2)+'\n'));
files.set('SHA256SUMS',Buffer.from([...files].filter(([p])=>!p.startsWith('third_party_notices/')).map(([p,b])=>sha(b)+'  '+p).join('\n')+'\n'));
fs.mkdirSync(out,{recursive:true});for(const[rel,b]of files){const to=path.join(out,rel);fs.mkdirSync(path.dirname(to),{recursive:true});fs.writeFileSync(to,b,{flag:'wx'});}
console.log(JSON.stringify({destination:out,files:files.size,private_patterns_checked:privateValues.length,images},null,2));
