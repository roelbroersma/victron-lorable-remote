// Create a clean publication tree; never copy the development workspace wholesale.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {fileURLToPath} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const version='4.11.1';
const buildRoot=path.resolve(process.env.LORABLE_BUILD_ROOT||root);
const out=path.resolve(process.argv[2]||path.join(root,'release_staging',version));
assert.ok(out!==root&&!root.startsWith(out+path.sep),'Choose a separate staging directory.');
assert.ok(!fs.existsSync(out)||fs.readdirSync(out).length===0,'Destination must be new or empty; nothing is overwritten.');
const stm=path.join(buildRoot,'build_public4111'),esp=path.join(buildRoot,'esp8684/build_release4111');
const options=JSON.parse(fs.readFileSync(path.join(stm,'build.options.json')));
assert.match(options.customBuildProperties,/-DLORABLE_PUBLIC_BUILD/);
const project=JSON.parse(fs.readFileSync(path.join(esp,'project_description.json')));
assert.equal(project.project_version,version);assert.equal(project.target,'esp32c2');
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const imagePath=path.join(buildRoot,'dist/release-'+version+'/LoRaBLE-Remote-'+version+'.bin');
const image=fs.readFileSync(imagePath);
assert.equal(image.subarray(0,8).toString(),'LBRUPD1\0');
assert.equal(image.subarray(16,48).toString().split('\0')[0],version);
const control=fs.readFileSync(path.join(stm,'stm32.ino.bin')),raw=fs.readFileSync(path.join(esp,'lorable_esp8684.bin'));
assert.equal(sha(control),image.subarray(64,96).toString('hex'),'Control build differs from the complete image.');
assert.equal(sha(raw),image.subarray(132,164).toString('hex'),'Connectivity build differs from the complete image.');
const localFile=path.join(buildRoot,'stm32/settings.local.h');
const local=fs.existsSync(localFile)?fs.readFileSync(localFile,'utf8'):'';
const privateValues=[];
for(const m of local.matchAll(/^\s*#define\s+(LORAWAN_(?:APPKEY|APPEUI|DEVEUI)|WIFI_AP_PASSWORD|VICTRON_MAC)\s+([^\r\n]+)/gm)){
 const quoted=m[2].match(/"([^"]+)"/);if(quoted&&quoted[1].length>=8)privateValues.push(Buffer.from(quoted[1]));
 const pairs=[...m[2].matchAll(/0x([0-9a-f]{2})\b/gi)].map(x=>parseInt(x[1],16));
 if(pairs.length>=8&&pairs.some(x=>x)){const b=Buffer.from(pairs);privateValues.push(b,Buffer.from(b.toString('hex')),Buffer.from(b.toString('hex').toUpperCase()));}
}
function scan(b,label){for(const secret of privateValues)assert.equal(b.includes(secret),false,'Private installation value found in '+label);}
const files=new Map();
function add(relative,source=path.join(root,relative)){const b=fs.readFileSync(source);scan(b,relative);files.set(relative.replaceAll('\\','/'),b);}
function flat(directory,predicate){for(const entry of fs.readdirSync(path.join(root,directory),{withFileTypes:true}))if(entry.isFile()&&predicate(entry.name))add(directory+'/'+entry.name);}
for(const f of ['README.md','README.en.md','Install.cmd','LICENSE','.gitignore','.gitattributes','THIRD-PARTY-NOTICES.md'])add(f);
for(const f of ['INSTALL.md','NETWORKS.md','EXAMPLES.md','DEVELOPMENT.md','RELEASE.md'])add('docs/'+f);
flat('stm32',n=>/\.(h|cpp|ino|js)$/.test(n)&&n!=='settings.local.h');
flat('esp8684/main',n=>/\.(c|h|txt)$/.test(n));
for(const f of ['esp8684/CMakeLists.txt','esp8684/partitions.csv','esp8684/sdkconfig.defaults','esp8684/build.ps1','web/index.html','arduino/platform.local.txt','updater/ram-loader.c','updater/ram-loader.ld','.github/workflows/release.yml'])add(f);
for(const f of ['First-Install.ps1','FirstInstall.Core.ps1','FirstInstall.Windows.cs','Start-First-Install.cmd','bootstrap-image.json','bootstrap/bootstrap.ino','Flash-USB.ps1','Usb-Portal.ps1','Bundle.ps1','Start-USB-Flash.cmd','START-HERE.md'])add('installer/'+f);
for(const f of ['build.ps1','build-first-install.ps1','build-ram-loader.ps1','embed-ram-loader.mjs','build-web.mjs','check-web-asset.mjs','pack-esp-ota.py','update-bundle.py','test-update-bundle.py','test-bundle-powershell.ps1','test-first-install.ps1','test-ram-loader.py','test-portal-socket-budget.py','test-codec.cjs','test-web.cjs','prepare-release.mjs','check-release.py','make-release-zip.py'])add('tools/'+f);
flat('tools/tests',n=>/\.(h|cpp)$/.test(n));flat('tools/tests/manager',n=>n.endsWith('.h'));
for(const name of ['status.png','networks.png','manage.png'])add('docs/images/'+name,path.join(root,'test-results',name));
add('firmware/LoRaBLE-Remote-'+version+'.bin',imagePath);
const helper=JSON.parse(files.get('installer/bootstrap-image.json'));
const helperRaw=Buffer.from(helper.image_base64,'base64');
assert.equal(sha(helperRaw),helper.sha256);scan(helperRaw,'decoded setup helper');scan(raw,'decoded connectivity application');
// Preserve canonical license texts, not similarly named SDK example source files.
function licenses(base,label){
 assert.ok(fs.existsSync(base),'SDK notice source missing: '+label);
 function walk(dir){for(const e of fs.readdirSync(dir,{withFileTypes:true})){
  if(e.isSymbolicLink()||['.git','build','node_modules','__pycache__'].includes(e.name))continue;
  const source=path.join(dir,e.name);
  if(e.isDirectory())walk(source);
  else if(/^(licen[sc]e|copying|notice|copyright)([._-][a-z0-9._-]+)?$/i.test(e.name)||/^(EXCEPTION|EXCEPTIONS)(\.txt)?$/.test(e.name)){
   if(fs.statSync(source).size<=200000&&!/\.(c|h|cpp|hpp)$/i.test(e.name))add('third_party_notices/'+label+'/'+path.relative(base,source),source);
  }
 }}walk(base);
}
licenses(process.env.LORABLE_RUI_ROOT||path.join(process.env.LOCALAPPDATA,'Arduino15/packages/rak_rui/hardware/stm32/4.2.4'),'RAK-RUI-4.2.4');
licenses(process.env.IDF_PATH||path.join(process.env.USERPROFILE,'.cache/esp-idf-v5.5.5'),'ESP-IDF-5.5.5');
for(const [base,label]of [[path.join(process.env.LOCALAPPDATA,'Arduino15/packages/rak_rui/tools/arm-none-eabi-gcc'),'ARM-toolchain'],[path.join(process.env.USERPROFILE,'.cache/espressif-v5.5.5-tools/tools/riscv32-esp-elf'),'RISC-V-toolchain']])if(fs.existsSync(base))licenses(base,label);
files.set('firmware/manifest.json',Buffer.from(JSON.stringify({version,target:'RAK11162',format:1,firmware:{path:'firmware/LoRaBLE-Remote-'+version+'.bin',bytes:image.length,sha256:sha(image)},installer_helper:{path:'installer/bootstrap-image.json',sha256:sha(files.get('installer/bootstrap-image.json')),image_sha256:helper.sha256}},null,2)+'\n'));
fs.mkdirSync(out,{recursive:true});
for(const [relative,b]of files){const destination=path.join(out,relative);fs.mkdirSync(path.dirname(destination),{recursive:true});fs.writeFileSync(destination,b,{flag:'wx'});}
console.log(JSON.stringify({destination:out,files:files.size,private_patterns_checked:privateValues.length,firmware_sha256:sha(image)},null,2));
