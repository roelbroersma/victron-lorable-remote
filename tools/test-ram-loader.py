"""Execute the actual Cortex-M4 RAM updater with modeled UART/flash (Unicorn 2.1.4)."""
from collections import deque
from pathlib import Path
import struct
import re
import zlib
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2

ROOT=Path(__file__).resolve().parent.parent
APP=0x08006000
LIMIT=0x08037000
UART=0x40013800
FLASH=0x58004000
def u32(n): return struct.pack('<I',n & 0xffffffff)

def run(mode=1, corrupt=False, invalid_size=False, fail_second_pass=False, length=4104):
    uc=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
    for addr,size in [(0x20000000,0x10000),(0x08000000,0x40000),(0x40013000,0x1000),(0x40003000,0x1000),(0x58004000,0x1000),(0xe0000000,0x100000)]:
        uc.mem_map(addr,size)
    embedded=(ROOT/'stm32/ram_loader_asset.h').read_text()
    uc.mem_write(0x20000000,bytes(int(value,16) for value in re.findall(r'0x([0-9a-fA-F]{2})',embedded)))
    uc.mem_write(0x08000000,b'\xa5'*0x40000)
    uc.mem_write(0x08004000,u32(0x5a5a5a5a)+u32(0x02020202)+b'\xff'*(2048-8))
    uc.mem_write(FLASH+0x14,u32(0x80000000))
    image=bytearray((i*37+11)&255 for i in range(length))
    struct.pack_into('<II',image,0,0x2000fc00,APP+0x101)
    expected=zlib.crc32(image)
    incoming=deque();outgoing=bytearray();events=[];erases=[];writes=[];ticks=0;reads=0;first_page_reads=0
    def read_hook(machine,access,address,size,value,user):
        nonlocal ticks,reads
        if address==UART+0x1c: machine.mem_write(address,u32(0x80|(0x20 if incoming else 0)))
        elif address==UART+0x24:
            assert incoming,'Unexpected UART read'
            machine.mem_write(address,u32(incoming.popleft()))
        elif address==0xe0001004:
            ticks=(ticks+480000)&0xffffffff;machine.mem_write(address,u32(ticks))
        elif address==FLASH+0x10: machine.mem_write(address,u32(0))
        reads+=1
    def write_hook(machine,access,address,size,value,user):
        nonlocal first_page_reads
        if address==UART+0x28:
            outgoing.append(value&255)
            if len(outgoing)==16:
                m,off,n,crc=struct.unpack('<IIII',outgoing)
                assert zlib.crc32(outgoing[:12])==crc
                outgoing.clear()
                if m==0x3152424c:
                    assert 0<=off<len(image) and 0<n<=2048 and off+n<=len(image)
                    if off==0:first_page_reads+=1
                    chunk=bytes(image[off:off+n])
                    if corrupt:chunk=bytes([chunk[0]^1])+chunk[1:]
                    if fail_second_pass and first_page_reads>=2:chunk=bytes([chunk[0]^1])+chunk[1:]
                    incoming.extend(struct.pack('<IIII',0x3144424c,off,n,zlib.crc32(image[off:off+n]))+chunk)
                else:
                    events.append((m,off,n))
                    if m==0x314b424c:
                        ack=struct.pack('<III',0x3141424c,off,1)
                        incoming.extend(b'boot startup\r\n'+ack+u32(zlib.crc32(ack)))
        elif address==FLASH+0x08 and value==0xcdef89ab:machine.mem_write(FLASH+0x14,u32(0))
        elif address==FLASH+0x14 and value&0x10000 and value&2:
            page=(value>>3)&0x7f;target=0x08000000+page*2048
            assert mode==1 and (target==0x08004000 or APP<=target<LIMIT)
            erases.append(target);machine.mem_write(target,b'\xff'*2048)
        elif 0x08000000<=address<0x08040000:
            assert mode==1 and (0x08004000<=address<0x08004800 or APP<=address<LIMIT)
            assert any(p<=address<p+2048 for p in erases)
            old=int.from_bytes(machine.mem_read(address,size),'little')
            assert old&value==value,'Programming non-erased flash bits'
            if address==APP:
                assert all(any(p==APP+i for p in erases) for i in range(0,len(image),2048)), 'Vector committed before all pages'
            writes.append(address)
        elif address==0xe000ed0c and value==0x05fa0004:machine.emu_stop()
    uc.hook_add(UC_HOOK_MEM_READ,read_hook)
    uc.hook_add(UC_HOOK_MEM_WRITE,write_hook)
    uc.reg_write(UC_ARM_REG_SP,0x2000fc00)
    uc.reg_write(UC_ARM_REG_R0,0x31008 if invalid_size else len(image))
    uc.reg_write(UC_ARM_REG_R1,expected);uc.reg_write(UC_ARM_REG_R2,mode)
    uc.emu_start(0x20000001,0,count=200_000_000)
    assert events,'Updater did not report a terminal result'
    result=events[-1]
    if invalid_size or corrupt:
        assert result[0]==0x3145424c and not erases and not writes
    elif fail_second_pass:
        assert result[0]==0x3145424c
        assert bytes(uc.mem_read(APP,8))==b'\xff'*8
        assert bytes(uc.mem_read(0x08004004,4))==u32(0x01010101)
    elif not mode:
        assert result==(0x3156424c,expected,0) and not erases and not writes
    else:
        assert result==(0x314b424c,expected,1),result
        assert bytes(uc.mem_read(APP,len(image)))==image
        assert bytes(uc.mem_read(0x08004004,4))==u32(0x02020202)
    assert bytes(uc.mem_read(0x08000000,0x4000))==b'\xa5'*0x4000
    assert bytes(uc.mem_read(0x08004800,0x1800))==b'\xa5'*0x1800
    assert bytes(uc.mem_read(LIMIT,0x9000))==b'\xa5'*0x9000
    print('PASS',dict(mode=mode,corrupt=corrupt,invalid_size=invalid_size,fail_second_pass=fail_second_pass,length=length,result=result[1],erases=len(erases)))

if __name__=='__main__':
    run(mode=0);run();run(corrupt=True);run(invalid_size=True);run(fail_second_pass=True)
    run(length=0x31000)
