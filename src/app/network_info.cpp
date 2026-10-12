#include "network_info.h"
#include "proxy_query.h"
#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>
#include <stdexcept>

namespace {
QString clean(const QJsonObject& object, const char* key) { return object.value(key).toString().trimmed().left(160); }
QString randomHex(int bytes) {
    QByteArray value; for (int i=0;i<bytes;++i) value.append(char(QRandomGenerator::system()->generate() & 255));
    return QString::fromLatin1(value.toHex());
}
QVariantMap traceInfo(const QByteArray& body) {
    QVariantMap result;
    for (const auto& line : body.split('\n')) {
        const auto equal = line.indexOf('='); if (equal < 0) continue;
        const auto key = line.left(equal); const auto value = QString::fromUtf8(line.mid(equal+1)).trimmed();
        if (key == "ip" && publicNetworkAddress(value)) result["ip"] = value;
        if (key == "loc" && QRegularExpression("^[A-Z]{2}$").match(value).hasMatch()) result["countryCode"] = value;
        if (key == "colo") result["colo"] = value.left(8);
    }
    if (!result.contains("ip")) throw std::runtime_error("出口 IP 查询暂不可用");
    result["protocol"] = QHostAddress(result["ip"].toString()).protocol() == QAbstractSocket::IPv6Protocol ? "IPv6" : "IPv4";
    return result;
}
}

QVariantMap network_info::location(const QByteArray& body, const QString& expectedIp) {
    QJsonParseError parse; const auto doc = QJsonDocument::fromJson(body,&parse);
    if (parse.error != QJsonParseError::NoError || !doc.isObject()) throw std::runtime_error("出口地区查询响应无效");
    const auto o = doc.object();
    if (QHostAddress(clean(o,"ip")) != QHostAddress(expectedIp) || o.contains("error")) throw std::runtime_error("出口地区查询地址不一致");
    QVariantMap result{{"ip",expectedIp},{"country",clean(o,"country_name")},{"countryCode",clean(o,"country_code").toUpper().remove(QRegularExpression("[^A-Z]" )).left(2)},
        {"provider",clean(o,"isp_name")},{"position",QStringList{clean(o,"city_name"),clean(o,"region_name")}.join(", ").remove(QRegularExpression("(^, |, $)"))},
        {"timezone",clean(o,"time_zone")}};
    const auto asn = o.value("as_number");
    if (asn.isDouble() && asn.toDouble() > 0) result["asn"] = "AS" + QString::number(quint64(asn.toDouble()));
    else if (asn.isString()) result["asn"] = "AS" + asn.toString().remove(QRegularExpression("^AS"));
    if (o.value("latitude").isDouble() && o.value("longitude").isDouble()) result["coordinates"] = QString::number(o.value("longitude").toDouble(),'f',2)+", "+QString::number(o.value("latitude").toDouble(),'f',2);
    return result;
}
QVariantList network_info::resolvers(const QByteArray& body, const QString& expectedSession) {
    QJsonParseError parse; const auto doc = QJsonDocument::fromJson(body,&parse);
    if (parse.error != QJsonParseError::NoError || !doc.isObject() || (!doc.object().value("ip").isObject() && !(doc.object().value("ip").isArray() && doc.object().value("ip").toArray().isEmpty()))) throw std::runtime_error("DNS 检测响应无效");
    if (expectedSession.isEmpty() || doc.object().value("session").toString()!=expectedSession) throw std::runtime_error("DNS 检测会话不一致");
    QVariantList result; const auto ips = doc.object().value("ip").toObject();
    for (auto i=ips.begin();i!=ips.end() && result.size()<16;++i) {
        if (!publicNetworkAddress(i.key()) || !i.value().isDouble() || i.value().toDouble() <= 0) continue;
        result.append(QVariantMap{{"ip",i.key()},{"count",i.value().toDouble()}});
    }
    return result;
}
QString network_info::assessment(const QVariantList& rows, const QString& exitCountry, bool complete) {
    if (rows.isEmpty()) return QStringLiteral("未能判断");
    if (!complete) return QStringLiteral("部分结果，未能完整判断");
    bool unknown = exitCountry.isEmpty();
    for (const auto& row : rows) {
        const auto country = row.toMap().value("countryCode").toString();
        if (country.isEmpty()) unknown = true;
        else if (!exitCountry.isEmpty() && country != exitCountry) return QStringLiteral("地区不一致，需核查");
    }
    return unknown ? QStringLiteral("已取得 DNS 出口") : QStringLiteral("未见地区异常");
}

void network_info::mergeResolvers(QVariantList& rows,const QVariantList& observed,const QString& probe) {
 for(const auto& entry:observed){auto row=entry.toMap();bool duplicate=false;
  for(const auto& current:rows){const auto previous=current.toMap();if(previous.value("ip")==row.value("ip") && previous.value("probe").toString()==probe){duplicate=true;break;}}
  if(!duplicate){row["probe"]=probe;rows.append(row);}
 }
}

QString network_info::dnsAssessment(const QVariantList& rows,const QString& exitCountry,int observedProbes,bool fakeIp,bool timedOut) {
 if(fakeIp && observedProbes<4) return rows.isEmpty()?QStringLiteral("Fake-IP，未取得真实 DNS 出口"):QStringLiteral("Fake-IP 连接观测，需核查");
 if(rows.isEmpty() && timedOut)return QStringLiteral("DNS 查询超时，未能判断");
 return assessment(rows,exitCountry,observedProbes==4);
}

NetworkInfo::NetworkInfo(bool demo,QObject* parent) : QObject(parent),demo_(demo) {}
NetworkInfo::~NetworkInfo() { if (cancellation_) *cancellation_=true; if (job_) { job_->wait(); delete job_; } }
void NetworkInfo::cancel() { if (cancellation_) *cancellation_=true; }
void NetworkInfo::invalidate() { cancel(); ++generation_; info_.clear(); dns_.clear(); error_.clear(); context_.clear(); refreshedAt_=0; dnsState_=QStringLiteral("未检测"); emit changed(); }
void NetworkInfo::copyIp() { const auto ip=info_.value("ip").toString(); if (!QHostAddress(ip).isNull()) QApplication::clipboard()->setText(ip); }
void NetworkInfo::finish() {
    connect(job_,&QThread::finished,this,[this] { auto* finished=job_; job_=nullptr; cancellation_.reset(); finished->deleteLater(); emit changed(); });
    job_->start(); emit changed();
}
void NetworkInfo::refresh(int mode,int protocol,quint32 index,const QString& source,bool force) {
    if (job_) return;
    const QString context=QString::number(mode)+":"+QString::number(protocol)+":"+QString::number(index)+":"+source;
    const auto now=QDateTime::currentMSecsSinceEpoch();
    if (!force && context==context_ && refreshedAt_ && now-refreshedAt_<600000) return;
    if (context!=context_) { info_.clear(); dns_.clear(); dnsState_=QStringLiteral("未检测"); }
    context_=context; error_.clear(); dns_.clear(); dnsState_=QStringLiteral("未检测");
    if (demo_) {
        info_={{"ip","192.0.2.86"},{"country","United States"},{"countryCode","US"},{"position","Los Angeles, California"},{"timezone","America/Los_Angeles"},
            {"provider","Example Networks"},{"asn","AS64496"},{"coordinates","-118.31, 34.06"},{"protocol",protocol==2?"IPv6":"IPv4"},{"route",mode?QStringLiteral("实体网卡直连"):QStringLiteral("系统路由")},{"source","speed.cloudflare.com"}};
        if (protocol==2) info_["ip"]="2001:db8::86";
        refreshedAt_=now; emit changed(); return;
    }
    cancellation_=std::make_shared<std::atomic_bool>(false); const auto cancelled=cancellation_; const auto generation=++generation_; dnsJob_=false;
    job_=QThread::create([=,this] {
        QVariantMap result; QString error;
        try {
            auto route=networkRoute(mode==1,index,source,protocol);
            NetworkResponse response;
            try { response=networkGet(route,"speed.cloudflare.com","/cdn-cgi/trace",*cancelled); }
            catch (const std::exception&) { if (*cancelled || protocol || !mode || route.protocol!=2) throw; route=networkRoute(true,index,source,1); response=networkGet(route,"speed.cloudflare.com","/cdn-cgi/trace",*cancelled); }
            result=traceInfo(response.body); result["route"]=response.route; result["source"]="speed.cloudflare.com";
            try {
                const auto geo=networkGet(route,"ipleak.net","/json/"+QUrl::toPercentEncoding(result["ip"].toString()),*cancelled);
                const auto location=network_info::location(geo.body,result["ip"].toString());
                for (auto i=location.begin();i!=location.end();++i) if (!i.value().toString().isEmpty()) result[i.key()]=i.value();
            } catch (const std::exception&) { if (*cancelled) throw; error=QStringLiteral("地区信息暂不可用"); }
        } catch (const std::exception& e) { error=*cancelled?QStringLiteral("已取消"):QString::fromUtf8(e.what()); }
        QMetaObject::invokeMethod(this,[this,generation,result,error] { if (generation!=generation_) return; info_=result; error_=error; refreshedAt_=result.isEmpty()?0:QDateTime::currentMSecsSinceEpoch(); emit changed(); },Qt::QueuedConnection);
    }); finish();
}
void NetworkInfo::detectDns(int mode,int protocol,quint32 index,const QString& source) {
    if (job_) return;
    dns_.clear(); dnsState_=QStringLiteral("检测中"); dnsScope_=mode?QStringLiteral("所选网卡 DNS"):QStringLiteral("系统 DNS");
    if (demo_) { dns_={QVariantMap{{"ip","198.51.100.53"},{"country","United States"},{"countryCode","US"},{"position","Los Angeles"},{"provider","Example DNS"},{"asn","AS64496"},{"probe",QStringLiteral("连接触发")}}}; dnsState_=QStringLiteral("演示结果"); emit changed(); return; }
    cancellation_=std::make_shared<std::atomic_bool>(false); const auto cancelled=cancellation_; const auto generation=++generation_; const auto country=info_.value("countryCode").toString(); dnsJob_=true;
    job_=QThread::create([=,this] {
        QVariantList rows; QString state;
        try {
            QElapsedTimer deadline; deadline.start();
            const auto remaining=[&](qint64 limit) { if (*cancelled) throw std::runtime_error("cancelled"); const auto left=45000-deadline.elapsed(); if (left<=0) throw std::runtime_error("DNS 检测超时"); return std::min<qint64>(limit,left); };
            auto physical=networkRoute(true,index,source,(!mode && !protocol)?1:protocol);
            // Resolve only the stable service host. Pin nonce HTTPS so reading
            // observations cannot add proxy-side nonce DNS queries to the test.
            QString pinned;
            try { pinned=networkResolvePhysical(physical,"ipleak.net",*cancelled,remaining(10000)); }
            catch(const std::exception&) { if(*cancelled || mode || protocol)throw; physical=networkRoute(true,index,source,2); pinned=networkResolvePhysical(physical,"ipleak.net",*cancelled,remaining(10000)); }
            const auto route=mode ? physical : networkRoute(false,index,source,protocol);
            int observedProbes=0; bool fake=false, timedOut=false;
            auto query=[&](bool aaaa,bool forwarded) {
                const QString session=randomHex(20),host=session+"-"+randomHex(4)+".ipleak.net";
                if (!forwarded && mode) {
                    NetworkRoute dnsRoute;
                    try { dnsRoute=networkRoute(true,index,source,1); } catch(const std::exception&) { dnsRoute=networkRoute(true,index,source,2); }
                    timedOut |= !networkObservePhysicalDns(dnsRoute,host,aaaa,*cancelled,remaining(5000));
                } else if (!forwarded) {
                    const auto response=observeSystemDns(host,0,aaaa,*cancelled,remaining(8000));
                    fake |= response.fakeIp; timedOut |= response.timeout;
                }
                if (forwarded) {
                    // Trigger normal connection resolution in a separate session. A later TLS failure
                    // does not erase an authoritative DNS observation already made.
                    try { networkGet(route,host,"/dnsdetection/",*cancelled,remaining(8000)); }
                    catch(const std::exception&) { if(*cancelled)throw; }
                }
                const auto reply=networkGet(route,host,"/dnsdetection/",*cancelled,remaining(10000),pinned);
                const auto observed=network_info::resolvers(reply.body,session);
                network_info::mergeResolvers(rows,observed,forwarded?QStringLiteral("连接触发"):aaaa?"AAAA":"A");
                return !observed.isEmpty();
            };
            for(int probe=0;probe<4;++probe) if(query(probe%2==1,false))++observedProbes;
            if(!mode && fake && observedProbes<4) query(false,true);
            for (auto& entry : rows) {
                try { const auto row=entry.toMap(); const auto probe=row.value("probe"); entry=network_info::location(networkGet(route,"ipleak.net","/json/"+QUrl::toPercentEncoding(row["ip"].toString()),*cancelled,remaining(5000)).body,row["ip"].toString()); auto enriched=entry.toMap(); enriched["probe"]=probe; entry=enriched; }
                catch (const std::exception&) { if (*cancelled) throw; }
            }
            state=network_info::dnsAssessment(rows,country,observedProbes,fake,timedOut);
        } catch (const std::exception& e) { if (qEnvironmentVariableIsSet("OPENLNKU_NETWORK_DIAGNOSTIC")) qWarning("DNS diagnostic: %s",e.what()); state=*cancelled?QStringLiteral("已取消"):QStringLiteral("未能判断"); }
        QMetaObject::invokeMethod(this,[this,generation,rows,state] { if (generation!=generation_) return; dns_=rows; dnsState_=state; emit changed(); },Qt::QueuedConnection);
    }); finish();
}
