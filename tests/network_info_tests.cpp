#include "../src/app/network_info.h"
#include <QtTest>
#include "../src/app/dns_packet.h"
#include "../src/app/proxy_query.h"
class NetworkInfoTests final:public QObject {
 Q_OBJECT
private slots:
 void validatesGeoEcho() {
  const QByteArray body=R"({"ip":"8.8.8.8","country_name":"United States","country_code":"US","isp_name":"Google","as_number":15169,"city_name":"Mountain View","region_name":"California","time_zone":"America/Los_Angeles"})";
  const auto geo=network_info::location(body,"8.8.8.8"); QCOMPARE(geo.value("asn").toString(),QString("AS15169")); QCOMPARE(geo.value("timezone").toString(),QString("America/Los_Angeles"));
  QVERIFY_THROWS_EXCEPTION(std::runtime_error,network_info::location(body,"1.1.1.1"));
  QVERIFY_THROWS_EXCEPTION(std::runtime_error,network_info::location("[]","8.8.8.8"));
 }
 void validatesSessionAndActualResolverSchema() {
  const QByteArray body=R"({"session":"abc","ip":{"8.8.8.8":2,"2606:4700:4700::1111":1,"192.168.1.1":3,"198.18.0.1":4,"2001:db8::1":1,"1.1.1.1":"yes","9.9.9.9":0}})";
  const auto rows=network_info::resolvers(body,"abc"); QCOMPARE(rows.size(),2);
  QVERIFY_THROWS_EXCEPTION(std::runtime_error,network_info::resolvers(body,"different"));
  QCOMPARE(network_info::resolvers(R"({"session":"abc","ip":[]})","abc").size(),0);
  QVERIFY_THROWS_EXCEPTION(std::runtime_error,network_info::resolvers(R"({"session":"abc","ip":[1]})","abc"));
  QCOMPARE(network_info::resolvers(R"({"session":"abc","ip":{}})","abc").size(),0);
 }
 void validatesDnsWireResponseAndQuestion() {
  const auto host=QString(40,'a')+"-"+QString(8,'b')+".ipleak.net";
  const auto request=dnsProbePacket(host,0x1234,false); QCOMPARE(request.left(12).toHex(),QByteArray("123401000001000000000000")); QCOMPARE(request.right(4).toHex(),QByteArray("00010001"));
  QCOMPARE(dnsProbePacket(host,0x1234,true).right(4).toHex(),QByteArray("001c0001"));
  auto reply=request;reply[2]=char(0x81);reply[3]=char(0x80);QVERIFY(dnsProbeResponse(reply,request));
  reply[3]=char(0x83);QVERIFY(dnsProbeResponse(reply,request)); // NXDOMAIN still reaches the resolver.
  reply[2]=char(0x83);QVERIFY(!dnsProbeResponse(reply,request)); // Truncation requires TCP.
  reply[2]=char(0x81);reply[0]=char(0);QVERIFY(!dnsProbeResponse(reply,request));
  reply[0]=request[0];reply[13]='c';QVERIFY(!dnsProbeResponse(reply,request));
  QVERIFY(!dnsProbeResponse(request.left(12),request));
  QVERIFY_THROWS_EXCEPTION(std::runtime_error,dnsProbePacket("unrelated.example",1,false));
 }
 void cancellationAndBudgetRejectBeforeNetworkAccess() {
  const auto host=QString(40,'a')+"-"+QString(8,'b')+".ipleak.net"; std::atomic_bool cancelled=true;
  QVERIFY_THROWS_EXCEPTION(std::runtime_error,networkObservePhysicalDns({true,1,"192.0.2.1",1},host,false,cancelled,100));
  QVERIFY_THROWS_EXCEPTION(std::runtime_error,observeSystemDns(host,0,false,cancelled,100));
  cancelled=false;QVERIFY_THROWS_EXCEPTION(std::runtime_error,networkObservePhysicalDns({true,1,"192.0.2.1",1},host,false,cancelled,0));
 }
 void sameResolverRetainsSeparateObservationSources() {
  QVariantList rows; const QVariantList observed{QVariantMap{{"ip","8.8.8.8"},{"count",1}}};
  network_info::mergeResolvers(rows,observed,"A"); network_info::mergeResolvers(rows,observed,QStringLiteral("连接触发"));
  network_info::mergeResolvers(rows,observed,"A"); QCOMPARE(rows.size(),2);
  QCOMPARE(rows[0].toMap().value("probe").toString(),QString("A"));
  QCOMPARE(rows[1].toMap().value("probe").toString(),QStringLiteral("连接触发"));
 }
 void fakeIpSupplementDoesNotBecomeSafetyVerdict() {
  const QVariantList rows{QVariantMap{{"countryCode","US"}}};
  QCOMPARE(network_info::dnsAssessment(rows,"US",0,true,false),QStringLiteral("Fake-IP 连接观测，需核查"));
  QCOMPARE(network_info::dnsAssessment({},"US",0,true,false),QStringLiteral("Fake-IP，未取得真实 DNS 出口"));
  QCOMPARE(network_info::dnsAssessment({},"US",0,false,true),QStringLiteral("DNS 查询超时，未能判断"));
  QCOMPARE(network_info::dnsAssessment(rows,"US",4,false,false),QStringLiteral("未见地区异常"));
 }
 void conservativeAssessment() {
  QCOMPARE(network_info::assessment({},"CN"),QStringLiteral("未能判断"));
  const QVariantList us{QVariantMap{{"countryCode","US"}}};
  QCOMPARE(network_info::assessment(us,"CN"),QStringLiteral("地区不一致，需核查"));
  QCOMPARE(network_info::assessment(us,"US"),QStringLiteral("未见地区异常"));
  QCOMPARE(network_info::assessment(us,"US",false),QStringLiteral("部分结果，未能完整判断"));
  QCOMPARE(network_info::assessment({},"US",false),QStringLiteral("未能判断"));
  QCOMPARE(network_info::assessment(us,""),QStringLiteral("已取得 DNS 出口"));
  QCOMPARE(network_info::assessment({QVariantMap{{"ip","8.8.8.8"}}},"CN"),QStringLiteral("已取得 DNS 出口"));
 }
 void demoContextAndInvalidationNeverNetwork() {
  NetworkInfo info(true);info.refresh(0,0,0,"invalid"); QVERIFY(!info.refreshing()); QCOMPARE(info.info().value("protocol").toString(),QString("IPv4"));
  info.detectDns(0,0,0,"invalid"); QCOMPARE(info.dnsState(),QStringLiteral("演示结果")); QVERIFY(!info.checkingDns());
  info.refresh(1,2,0,"invalid"); QCOMPARE(info.info().value("protocol").toString(),QString("IPv6")); QVERIFY(info.dns().isEmpty());
  info.invalidate(); QVERIFY(info.info().isEmpty()); QVERIFY(info.dns().isEmpty()); QCOMPARE(info.dnsState(),QStringLiteral("未检测"));
 }
};
QTEST_GUILESS_MAIN(NetworkInfoTests)
#include "network_info_tests.moc"
