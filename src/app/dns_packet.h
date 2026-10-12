#pragma once
#include <QByteArray>
#include <QString>
#include <QRegularExpression>
#include <stdexcept>
inline QByteArray dnsProbePacket(const QString& host,quint16 transaction,bool aaaa) {
 if(!QRegularExpression("^[a-f0-9]{40}-[a-f0-9]{8}\\.ipleak\\.net$").match(host).hasMatch())throw std::runtime_error("无效 DNS 检测请求");
 QByteArray packet;const auto append16=[&](quint16 n){packet.append(char(n>>8));packet.append(char(n&255));};
 append16(transaction);append16(0x0100);append16(1);append16(0);append16(0);append16(0);
 for(const auto& label:host.toLatin1().split('.')){packet.append(char(label.size()));packet.append(label);}packet.append(char(0));append16(aaaa?28:1);append16(1);return packet;
}
inline bool dnsProbeResponse(const QByteArray& response,const QByteArray& request) {
 if(request.size()<12 || response.size()<request.size())return false;
 const auto word=[&](int i){return quint16((quint8(response[i])<<8)|quint8(response[i+1]));};
 const auto flags=word(2);
 return response.left(2)==request.left(2) && (flags&0x8000) && !(flags&0x7800) && !(flags&0x0200) && (flags&0x000f)<=3 && word(4)==1 && response.mid(12,request.size()-12)==request.mid(12);
}
