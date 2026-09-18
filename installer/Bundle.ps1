# Strict validation before opening USB or transmitting any firmware bytes.
if(-not ('LoRaBLE.Bundle' -as [type])){
 Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.Text;
using System.Text.RegularExpressions;
using System.Security.Cryptography;
namespace LoRaBLE {
 public sealed class Bundle {
  public byte[] Bytes, Control, Connectivity;
  public string Version, Sha256;
  static void Require(bool ok,string text){if(!ok)throw new InvalidDataException(text);}
  static uint U32(byte[] b,int o){return BitConverter.ToUInt32(b,o);}
  static byte[] Part(byte[] b,int o,int n){var p=new byte[n];Array.Copy(b,o,p,0,n);return p;}
  static bool Equal(byte[] a,byte[] b){if(a.Length!=b.Length)return false;int d=0;for(int i=0;i<a.Length;i++)d|=a[i]^b[i];return d==0;}
  static bool Zero(byte[] b,int o,int n){for(int i=o;i<o+n;i++)if(b[i]!=0)return false;return true;}
  public static uint Crc(byte[] b,int o,int n){return CrcUpdate(0,b,o,n);}
  static uint CrcUpdate(uint seed,byte[] b,int o,int n){uint c=~seed;for(int i=o;i<o+n;i++){c^=b[i];for(int j=0;j<8;j++)c=(c>>1)^((c&1)!=0?0xedb88320u:0u);}return ~c;}
  public static byte[] Packet(byte[] b,int offset,int length,uint sequence){
   var p=new byte[16+length];Array.Copy(BitConverter.GetBytes(0x3255424cu),0,p,0,4);
   Array.Copy(BitConverter.GetBytes(sequence),0,p,4,4);Array.Copy(BitConverter.GetBytes((uint)length),0,p,8,4);
   Array.Copy(b,offset,p,16,length);uint crc=CrcUpdate(Crc(p,0,12),p,16,length);
   Array.Copy(BitConverter.GetBytes(crc),0,p,12,4);return p;
  }
  public static int Ack(byte[] b,uint sequence){
   if(b.Length!=16||U32(b,0)!=0x3241424cu||U32(b,4)!=sequence||Crc(b,0,12)!=U32(b,12))return -1;
   return (int)U32(b,8);
  }
  static byte[] Hash(byte[] b){using(var h=SHA256.Create())return h.ComputeHash(b);}
  public static Bundle Read(string path){
   var info=new FileInfo(path);Require(info.Length>=600&&info.Length<=0xd6100,"Invalid complete firmware size.");
   var b=File.ReadAllBytes(path);Require(Encoding.ASCII.GetString(b,0,8)=="LBRUPD1\0"&&U32(b,8)==1&&U32(b,12)==11162,"Expected complete LoRaBLE RAK11162 firmware, not a component image.");
   string v=Encoding.ASCII.GetString(b,16,32).Split('\0')[0];
   Require(Regex.IsMatch(v,@"\A[0-9][0-9a-z.-]{0,30}\z")&&Zero(b,16+v.Length,32-v.Length),"Invalid firmware version.");
   Require(U32(b,60)==0&&Zero(b,164,88)&&Crc(b,0,252)==U32(b,252),"Invalid firmware header checksum or flags.");
   uint sl=U32(b,48),el=U32(b,52),raw=U32(b,128);
   Require(sl>=256&&sl<=0x31000&&sl%8==0&&el>=100&&el<=0xa5000&&raw>=288&&raw<=0xf0000&&256L+sl+el==b.Length,"Invalid component lengths.");
   byte[] s=Part(b,256,(int)sl),e=Part(b,256+(int)sl,(int)el);
   uint sp=U32(s,0),entry=U32(s,4);
   Require(sp>=0x20000000&&sp<=0x20010000&&sp%8==0&&(entry&1)==1&&entry>=0x08006000&&entry<0x08006000+sl,"Invalid application vectors.");
   Require(Crc(s,0,s.Length)==U32(b,56)&&Equal(Hash(s),Part(b,64,32))&&Equal(Hash(e),Part(b,96,32)),"Firmware content checksum mismatch.");
   Require(Encoding.ASCII.GetString(e,0,4)=="ESP\0"&&e[4]==2&&e[5]==1&&e[6]==0&&e[7]==0&&U32(e,40)+88L==el&&Zero(e,60,24)&&Crc(e,0,84)==U32(e,84),"Unsupported connectivity package.");
   using(var md5=MD5.Create())Require(Equal(md5.ComputeHash(e,88,e.Length-88),Part(e,44,16)),"Connectivity package checksum mismatch.");
   // Release builder validates decoded C2 image/SHA; retained bootloader decodes XZ.
   Require(Equal(Part(e,88,6),new byte[]{0xfd,0x37,0x7a,0x58,0x5a,0x00})&&e[94]==0&&e[95]==1,"Expected XZ/CRC32 package.");
   return new Bundle{Bytes=b,Control=s,Connectivity=e,Version=v,Sha256=BitConverter.ToString(Hash(b)).Replace("-","").ToLowerInvariant()};
  }
 }
}
'@
}
