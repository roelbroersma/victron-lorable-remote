// MIT, Copyright (c) 2026 Roel Broersma.
// Built-in Windows APIs only; no Python, Arduino IDE or extra runtime required.
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Runtime.InteropServices;
using System.Security;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace LoRaBLE.Install {
 public sealed class WifiInterface {
  public Guid Id; public string Name,Profile,Ssid; public int State;
 }
 public sealed class Wifi : IDisposable {
  IntPtr handle;
  [StructLayout(LayoutKind.Sequential,CharSet=CharSet.Unicode)]
  struct Info {public Guid Id;[MarshalAs(UnmanagedType.ByValTStr,SizeConst=256)] public string Name;public int State;}
  [StructLayout(LayoutKind.Sequential,CharSet=CharSet.Unicode)]
  struct ConnectParameters {public int Mode;[MarshalAs(UnmanagedType.LPWStr)]public string Profile;public IntPtr Ssid,Bssid;public int Type;public uint Flags;}
  [DllImport("wlanapi.dll")] static extern uint WlanOpenHandle(uint version,IntPtr reserved,out uint negotiated,out IntPtr h);
  [DllImport("wlanapi.dll")] static extern uint WlanCloseHandle(IntPtr h,IntPtr reserved);
  [DllImport("wlanapi.dll")] static extern uint WlanEnumInterfaces(IntPtr h,IntPtr reserved,out IntPtr list);
  [DllImport("wlanapi.dll")] static extern void WlanFreeMemory(IntPtr data);
  [DllImport("wlanapi.dll")] static extern uint WlanQueryInterface(IntPtr h,ref Guid id,int opcode,IntPtr reserved,out uint size,out IntPtr data,out int valueType);
  [DllImport("wlanapi.dll",CharSet=CharSet.Unicode)] static extern uint WlanSetProfile(IntPtr h,ref Guid id,uint flags,string xml,string access,[MarshalAs(UnmanagedType.Bool)]bool overwrite,IntPtr reserved,out uint reason);
  [DllImport("wlanapi.dll",CharSet=CharSet.Unicode)] static extern uint WlanDeleteProfile(IntPtr h,ref Guid id,string name,IntPtr reserved);
  [DllImport("wlanapi.dll",CharSet=CharSet.Unicode)] static extern uint WlanConnect(IntPtr h,ref Guid id,ref ConnectParameters p,IntPtr reserved);
  [DllImport("wlanapi.dll")] static extern uint WlanDisconnect(IntPtr h,ref Guid id,IntPtr reserved);
  static void Check(uint code){if(code!=0)throw new Win32Exception((int)code);}
  public Wifi(){uint v;Check(WlanOpenHandle(2,IntPtr.Zero,out v,out handle));}
  public WifiInterface[] Interfaces(){
   IntPtr list;Check(WlanEnumInterfaces(handle,IntPtr.Zero,out list));
   try{
    int count=Marshal.ReadInt32(list);if(count<0||count>64)throw new InvalidDataException("Invalid WiFi interface list");
    var result=new List<WifiInterface>();int stride=Marshal.SizeOf(typeof(Info));
    for(int i=0;i<count;i++){
     var info=(Info)Marshal.PtrToStructure(IntPtr.Add(list,8+i*stride),typeof(Info));
     var item=new WifiInterface{Id=info.Id,Name=info.Name,State=info.State,Profile="",Ssid=""};
     IntPtr data;uint size;int kind;Guid id=info.Id;
     uint rc=WlanQueryInterface(handle,ref id,7,IntPtr.Zero,out size,out data,out kind);
     // A connected interface without its profile cannot be restored safely.
     // Windows location/privacy policy may deny this query: fail before changes.
     if(rc!=0&&info.State==1)Check(rc);
     if(rc==0){try{
       if(size<556&&info.State==1)throw new InvalidDataException("Cannot capture the original WiFi connection");
       if(size>=556){item.Profile=Marshal.PtrToStringUni(IntPtr.Add(data,8),256).TrimEnd('\0');
        int n=Marshal.ReadInt32(data,520);if(n>=0&&n<=32){var b=new byte[n];Marshal.Copy(IntPtr.Add(data,524),b,0,n);item.Ssid=Encoding.UTF8.GetString(b);}}
      }finally{WlanFreeMemory(data);}}
     if(info.State==1&&String.IsNullOrEmpty(item.Profile))throw new InvalidDataException("Original WiFi profile unavailable; enable Windows WiFi/location access first");
     result.Add(item);
    }
    return result.ToArray();
   }finally{WlanFreeMemory(list);}
  }
  public static string ProfileXml(string name,string ssid,string password){
   if(String.IsNullOrEmpty(name)||name.Length>100||Encoding.UTF8.GetByteCount(ssid)>32||password.Length<8||password.Length>63)throw new ArgumentException("Invalid temporary WiFi profile");
   string hex=BitConverter.ToString(Encoding.UTF8.GetBytes(ssid)).Replace("-","");
   return "<?xml version=\"1.0\"?><WLANProfile xmlns=\"http://www.microsoft.com/networking/WLAN/profile/v1\"><name>"+SecurityElement.Escape(name)+
    "</name><SSIDConfig><SSID><hex>"+hex+"</hex></SSID></SSIDConfig><connectionType>ESS</connectionType><connectionMode>manual</connectionMode><MSM><security>"+
    "<authEncryption><authentication>WPA2PSK</authentication><encryption>AES</encryption><useOneX>false</useOneX></authEncryption><sharedKey><keyType>passPhrase</keyType><protected>false</protected><keyMaterial>"+
    SecurityElement.Escape(password)+"</keyMaterial></sharedKey></security></MSM></WLANProfile>";
  }
  public void AddTemporary(Guid id,string name,string ssid,string password){uint reason;Check(WlanSetProfile(handle,ref id,2,ProfileXml(name,ssid,password),null,false,IntPtr.Zero,out reason));}
  public void Connect(Guid id,string name){var p=new ConnectParameters{Mode=0,Profile=name,Type=1};Check(WlanConnect(handle,ref id,ref p,IntPtr.Zero));}
  public void Disconnect(Guid id){Check(WlanDisconnect(handle,ref id,IntPtr.Zero));}
  public void DeleteTemporary(Guid id,string name){if(!name.StartsWith("LoRaBLE-Install-",StringComparison.Ordinal))throw new ArgumentException("Not an installer profile");Check(WlanDeleteProfile(handle,ref id,name,IntPtr.Zero));}
  public void Dispose(){if(handle!=IntPtr.Zero){WlanCloseHandle(handle,IntPtr.Zero);handle=IntPtr.Zero;}}
 }

 // Read-only HTTP server, exact IP bind, one permitted peer, unguessable paths.
 // Firmware stays in memory. It is never loaded from a client-supplied filename.
 public sealed class FirmwareServer : IDisposable {
  readonly TcpListener listener;readonly IPAddress peer;readonly byte[] image;readonly string token;
  readonly Task worker;volatile bool stopping;TcpClient active;long sent;int complete,health;
  public long BytesSent{get{return Interlocked.Read(ref sent);}}
  public int CompletedRequests{get{return Volatile.Read(ref complete);}}
  public int HealthRequests{get{return Volatile.Read(ref health);}}
  public int Port{get{return ((IPEndPoint)listener.LocalEndpoint).Port;}}
  public FirmwareServer(string address,string allowedPeer,int port,string secret,byte[] firmware){
   if(!System.Text.RegularExpressions.Regex.IsMatch(secret,"\\A[0-9a-f]{32}\\z")||firmware==null||firmware.Length<100||firmware.Length>0xa5000)throw new ArgumentException("Invalid firmware server inputs");
   IPAddress bind=IPAddress.Parse(address);peer=IPAddress.Parse(allowedPeer);
   if(bind.Equals(IPAddress.Any)||bind.Equals(IPAddress.IPv6Any)||bind.AddressFamily!=AddressFamily.InterNetwork||peer.AddressFamily!=AddressFamily.InterNetwork)throw new ArgumentException("Explicit IPv4 addresses required");
   token=secret;image=(byte[])firmware.Clone();listener=new TcpListener(bind,port);listener.Start(2);
   worker=Task.Run((Action)Run);
  }
  void Run(){
   while(!stopping){try{
     using(var c=listener.AcceptTcpClient()){
      active=c;if(!((IPEndPoint)c.Client.RemoteEndPoint).Address.Equals(peer))continue;
      c.ReceiveTimeout=5000;c.SendTimeout=15000;c.NoDelay=true;
      var stream=c.GetStream();var header=new MemoryStream();int last=0;
      while(header.Length<4096){int b=stream.ReadByte();if(b<0)break;header.WriteByte((byte)b);last=(last<<8)|b;if(last==0x0d0a0d0a)break;}
      if(last!=0x0d0a0d0a)continue;
      string first=Encoding.ASCII.GetString(header.ToArray()).Split(new[]{"\r\n"},StringSplitOptions.None)[0];
      string[] request=first.Split(' ');if(request.Length!=3||request[0]!="GET"||!request[2].StartsWith("HTTP/1.",StringComparison.Ordinal)){Reply(stream,405,new byte[0],false);continue;}
      if(request[1]=="/"+token+"/health"){
       Reply(stream,200,Encoding.ASCII.GetBytes("LORABLE_OTA_READY:"+token),false);Interlocked.Increment(ref health);
      }else if(request[1]=="/"+token+"/firmware"){
       Interlocked.Exchange(ref sent,0);Reply(stream,200,image,true);Interlocked.Increment(ref complete);
      }else Reply(stream,404,new byte[0],false);
     }
    }catch(IOException){}catch(SocketException){if(stopping)break;}catch(ObjectDisposedException){if(stopping)break;}
    finally{active=null;}
   }
  }
  void Reply(NetworkStream stream,int code,byte[] body,bool track){
   byte[] header=Encoding.ASCII.GetBytes("HTTP/1.1 "+code+" "+(code==200?"OK":"Refused")+"\r\nContent-Type: application/octet-stream\r\nCache-Control: no-store\r\nContent-Length: "+body.Length+"\r\nConnection: close\r\n\r\n");
   stream.Write(header,0,header.Length);
   for(int off=0;off<body.Length;off+=4096){int n=Math.Min(4096,body.Length-off);stream.Write(body,off,n);if(track)Interlocked.Add(ref sent,n);}
   stream.Flush();
  }
  public void Dispose(){stopping=true;listener.Stop();var c=active;if(c!=null)c.Close();try{worker.Wait(3000);}catch(AggregateException){}Array.Clear(image,0,image.Length);}
 }
}
