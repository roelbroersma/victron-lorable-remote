const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const context={};vm.createContext(context);vm.runInContext(fs.readFileSync(__dirname+'/../stm32/lorawan-payload-codec.js','utf8'),context);
const base=[4,9,0,1,1,1,0,4,2,1,7,0,255,255,2,6,4,3,3,1];
let d=context.Decode(10,base);assert.equal(d.batteryprotect_on_verified,true);assert.equal(d.load_control_mode,null);assert.equal(d.load_always_on_verified,false);assert.equal(d.bluetooth_function,1);assert.equal(d.board_vbat_v,null);
let off=base.slice();off[5]=0;off[17]=4;off[19]=10;d=context.Decode(10,off);assert.equal(d.batteryprotect_off_verified,true);assert.equal(d.bluetooth_function,10);assert.equal(d.ble_function_2_requested,false);
let transient=base.slice();transient[5]=3;assert.equal(context.Decode(10,transient).batteryprotect_on_verified,false);
let failed=base.slice();failed[3]=9;assert.equal(context.Decode(10,failed).batteryprotect_on_verified,false);
for(const profile of [3,4])for(const state of [0,1,3,255])for(const result of [1,7,9]) {
 const packet=base.slice();packet[18]=profile;packet[5]=state;packet[3]=result;
 const decoded=context.Decode(10,packet),ttn=context.decodeUplink({fPort:10,bytes:packet}).data;
 assert.equal(decoded.device_profile,profile);assert.equal(decoded.load_control_mode,null);
 assert.equal(decoded.batteryprotect_output_state,state===255?null:state);
 assert.equal(decoded.batteryprotect_on_verified,state===1&&result===1);
 assert.equal(decoded.batteryprotect_off_verified,state===0&&result===1);
 assert.equal(ttn.batteryprotect_on_verified,decoded.batteryprotect_on_verified);
 assert.equal(ttn.batteryprotect_off_verified,decoded.batteryprotect_off_verified);
}
let mppt=base.slice();mppt[18]=1;mppt[5]=4;assert.equal(context.Decode(10,mppt).load_always_on_verified,true);
let generic=base.slice();generic[18]=2;assert.equal(context.Decode(10,generic).load_always_on_verified,false);
assert.equal(context.decodeUplink({fPort:10,bytes:base}).data.batteryprotect_on_verified,true);
for(const n of [0,1,7])assert.ok(context.decodeUplink({fPort:10,bytes:base.slice(0,n)}).errors);
for(const p of [0,224,1.1])assert.ok(context.decodeUplink({fPort:p,bytes:base}).errors);
assert.equal(context.Decode(10,[3,1,0,1,1,4,0,1]).load_always_on_verified,true);
console.log('PASS: Milesight/TTN codec, A3B1/A3B3 ON/OFF/transient/unknown/failure, function 10, MPPT/Generic isolation, legacy and input guards.');
