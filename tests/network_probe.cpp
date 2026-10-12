#include "../src/app/network_info.h"
#include "../src/app/proxy_query.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QTextStream>
#include <QTimer>
#include <QElapsedTimer>
int main(int argc,char** argv) {
 const int internal=runInternalProxyQuery(argc,argv); if (internal>=0) return internal;
 QCoreApplication app(argc,argv); QCommandLineParser p; p.addHelpOption();
 p.addOptions({{"network-probe","Explicitly allow metadata requests"},{"dns","Observe Windows DNS"},{"cancel-after-ms","Cancel DNS test after milliseconds","ms"},
 {"mode","direct or system","mode","direct"},{"protocol","auto, 4 or 6","protocol","auto"},
 {"interface-index","Selected physical interface","index"},{"source-ip","Selected interface IPv4","IP"}}); p.process(app);
 if (!p.isSet("network-probe")) return 64;
 const auto protocol=p.value("protocol"),mode=p.value("mode");
 if ((protocol!="auto"&&protocol!="4"&&protocol!="6") || (mode!="direct"&&mode!="system")) return 64;
 NetworkInfo info(false); bool started=false,dnsStarted=false,cancelScheduled=false; QElapsedTimer cancelClock;
 const int cancelDelay=p.value("cancel-after-ms").toInt(); if(p.isSet("cancel-after-ms") && (!p.isSet("dns") || cancelDelay<1 || cancelDelay>5000))return 64;
 const auto index=p.value("interface-index").toUInt(); const auto source=p.value("source-ip");
 QObject::connect(&info,&NetworkInfo::changed,&app,[&]{
  if(info.checkingDns() && cancelDelay && !cancelScheduled){cancelScheduled=true;QTimer::singleShot(cancelDelay,&info,[&]{cancelClock.start();info.cancel();});}
  if (!started || info.refreshing() || info.checkingDns()) return;
  if (p.isSet("dns") && !dnsStarted) { dnsStarted=true; QTimer::singleShot(0,&info,[&]{info.detectDns(mode=="direct",protocol=="4"?1:protocol=="6"?2:0,index,source);}); return; }
  int enriched=0,forwarded=0; for(const auto& entry:info.dns()){const auto row=entry.toMap();if(!row.value("countryCode").toString().isEmpty() && !row.value("provider").toString().isEmpty())++enriched;if(row.value("probe").toString()==QStringLiteral("连接触发"))++forwarded;}
  QTextStream(stdout)<<"PROTOCOL="<<info.info().value("protocol").toString()<<"\nGEO_OK="<<!info.info().value("timezone").toString().isEmpty()<<"\nEXIT_OK="<<!info.info().value("ip").toString().isEmpty()<<"\nERROR="<<info.error()<<"\nDNS_STATE="<<info.dnsState()<<"\nDNS_RESOLVERS="<<info.dns().size()<<"\nDNS_ENRICHED="<<enriched<<"\nDNS_CONNECTION_TRIGGERED="<<forwarded<<"\n";
  if(cancelDelay){QTextStream(stdout)<<"CANCEL_LATENCY_MS="<<cancelClock.elapsed()<<"\n";app.exit(info.dnsState()==QStringLiteral("已取消") && cancelClock.elapsed()<2000?0:3);return;}
  app.exit(p.isSet("dns") ? (info.dns().isEmpty()?3:0) : (info.info().isEmpty()?3:0));
 });
 QTimer::singleShot(80000,&app,[&]{info.cancel();app.exit(124);});
 started=true; info.refresh(mode=="direct",protocol=="4"?1:protocol=="6"?2:0,index,source,true);
 return app.exec();
}
