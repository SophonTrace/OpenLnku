#include "network_transport.h"
#include "dns_packet.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <QHostAddress>
#include <QRandomGenerator>
#include <QElapsedTimer>
#include <QRegularExpression>
#include <vector>
#include <cstring>
#include <stdexcept>
namespace {
struct Winsock { Winsock(){WSADATA d{};if(WSAStartup(MAKEWORD(2,2),&d))throw std::runtime_error("Windows 网络初始化失败");} ~Winsock(){WSACleanup();} };
struct Socket { SOCKET value; ~Socket(){if(value!=INVALID_SOCKET)closesocket(value);} };
}
// Observe the adapter's configured resolvers, never substitute a public resolver.
// A/AAAA is the record type; DNS transport uses an explicitly bound adapter IP.
bool networkObservePhysicalDns(const NetworkRoute& route,const QString& host,bool aaaa,std::atomic_bool& cancelled,qint64 budgetMs) {
 if (!route.direct || !route.index || budgetMs<=0 || !QRegularExpression("^[a-f0-9]{40}-[a-f0-9]{8}\\.ipleak\\.net$").match(host).hasMatch()) throw std::runtime_error("无效 DNS 检测请求");
 if(cancelled)throw std::runtime_error("cancelled");
 Winsock initialized; QElapsedTimer clock; clock.start();
 ULONG size=16384; std::vector<unsigned char> buffer(size);
 auto status=GetAdaptersAddresses(AF_UNSPEC,GAA_FLAG_SKIP_ANYCAST|GAA_FLAG_SKIP_MULTICAST,nullptr,reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()),&size);
 if(status==ERROR_BUFFER_OVERFLOW){buffer.resize(size);status=GetAdaptersAddresses(AF_UNSPEC,GAA_FLAG_SKIP_ANYCAST|GAA_FLAG_SKIP_MULTICAST,nullptr,reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()),&size);}
 if(status!=NO_ERROR) throw std::runtime_error("无法读取网卡 DNS");
 const QHostAddress source(route.source); const bool v6=source.protocol()==QAbstractSocket::IPv6Protocol;
 const auto packet=dnsProbePacket(host,quint16(QRandomGenerator::system()->generate()),aaaa);
 for(auto* adapter=reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());adapter;adapter=adapter->Next){
  if(adapter->IfIndex!=route.index && adapter->Ipv6IfIndex!=route.index)continue;
  int servers=0;
  for(auto* dns=adapter->FirstDnsServerAddress;dns && servers<3;dns=dns->Next){
   if(cancelled)throw std::runtime_error("cancelled");if(clock.elapsed()>=budgetMs)return false;
   const int family=v6?AF_INET6:AF_INET; if(!dns->Address.lpSockaddr || dns->Address.lpSockaddr->sa_family!=family)continue; ++servers;
   for (bool tcp : {false,true}) {
    if(cancelled)throw std::runtime_error("cancelled");if(clock.elapsed()>=budgetMs)return false;
    Socket socket{WSASocketW(family,tcp?SOCK_STREAM:SOCK_DGRAM,tcp?IPPROTO_TCP:IPPROTO_UDP,nullptr,0,WSA_FLAG_OVERLAPPED)};
    if(socket.value==INVALID_SOCKET)throw std::runtime_error("无法创建 DNS 检测连接");
    sockaddr_storage local{},peer{};const int length=v6?sizeof(sockaddr_in6):sizeof(sockaddr_in);std::memcpy(&peer,dns->Address.lpSockaddr,length);
    if(v6){auto* l=reinterpret_cast<sockaddr_in6*>(&local);l->sin6_family=AF_INET6;const auto ip=source.toIPv6Address();std::memcpy(&l->sin6_addr,ip.c,16);reinterpret_cast<sockaddr_in6*>(&peer)->sin6_port=htons(53);}
    else{auto* l=reinterpret_cast<sockaddr_in*>(&local);l->sin_family=AF_INET;l->sin_addr.s_addr=htonl(source.toIPv4Address());reinterpret_cast<sockaddr_in*>(&peer)->sin_port=htons(53);}
    const DWORD affinity=v6?route.index:htonl(route.index);
    if(setsockopt(socket.value,v6?IPPROTO_IPV6:IPPROTO_IP,v6?IPV6_UNICAST_IF:IP_UNICAST_IF,reinterpret_cast<const char*>(&affinity),sizeof(affinity)) || bind(socket.value,reinterpret_cast<sockaddr*>(&local),length))throw std::runtime_error("无法绑定所选网卡 DNS");
    u_long nonblocking=1;if(ioctlsocket(socket.value,FIONBIO,&nonblocking))throw std::runtime_error("无法启用可取消的 DNS 检测");
    const auto end=std::min<qint64>(budgetMs,clock.elapsed()+(tcp?1500:1200));
    const auto wait=[&](bool writing){while(clock.elapsed()<end){if(cancelled)throw std::runtime_error("cancelled");fd_set ready;FD_ZERO(&ready);FD_SET(socket.value,&ready);timeval timeout{0,100000};const int n=select(0,writing?nullptr:&ready,writing?&ready:nullptr,nullptr,&timeout);if(n<0)return false;if(n)return true;}return false;};
    if(connect(socket.value,reinterpret_cast<sockaddr*>(&peer),length)){
      if(WSAGetLastError()!=WSAEWOULDBLOCK || !wait(true))continue;
      int error=0,size=sizeof(error);if(getsockopt(socket.value,SOL_SOCKET,SO_ERROR,reinterpret_cast<char*>(&error),&size) || error)continue;
    }
    QByteArray wire;if(tcp){wire.append(char(packet.size()>>8));wire.append(char(packet.size()&255));}wire+=packet;
    int sent=0;while(sent<wire.size() && wait(true)){const auto n=send(socket.value,wire.constData()+sent,int(wire.size())-sent,0);if(n<0){if(WSAGetLastError()==WSAEWOULDBLOCK)continue;break;}if(!n)break;sent+=n;}
    if(sent!=wire.size())continue;
    QByteArray response;
    while(wait(false)){
      char bytes[4096];const int count=recv(socket.value,bytes,sizeof(bytes),0);if(count<=0)break;
      if(tcp){response.append(bytes,count);if(response.size()<2)continue;const int wanted=(quint8(response[0])<<8)|quint8(response[1]);if(wanted<12 || wanted>4096)break;if(response.size()<wanted+2)continue;response=response.mid(2,wanted);}else response=QByteArray(bytes,count);
      if(response.size()<12)continue;
      if(dnsProbeResponse(response,packet))return true;
      break;
    }
   }
  }
  break;
 }
 return false;
}
