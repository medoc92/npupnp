#include "config.h"

#include "upnpdescription.h"

#include <algorithm>
#include <cstring>
#include <string>

#ifdef USE_EXPAT
#include "expatmm.h"
#define XMLPARSERTP inputRefXMLParser
#else
#include "picoxml.h"
#define XMLPARSERTP PicoXMLParser
#endif

#include "genut.h"

using namespace std;

class UPnPDeviceParser : public XMLPARSERTP {
public:
    UPnPDeviceParser(const string& input, UPnPDeviceDesc& device)
        : XMLPARSERTP(input), m_device(device) {}

protected:
    void EndElement(const XML_Char *name) override {
        trimstring(m_chardata, " \t\n\r");

        // If deviceList is in the current tag path, this is an embedded device.
        // Arghh: upmpdcli wrongly used devicelist instead of
        // deviceList. Support both as it is unlikely that anybody
        // would use both for different purposes
        bool ismain = !std::any_of(
            m_path.begin(), m_path.end(),
            [](const StackEl& el) {
                return !stringlowercmp("devicelist", el.name);});

        UPnPDeviceDesc* dev = ismain ? &m_device : &m_tdevice;

        if (!strcmp(name, "service")) {
            dev->services.push_back(std::move(m_tservice));
            m_tservice = UPnPServiceDesc();
        } else if (!strcmp(name, "device")) {
            if (!ismain) {
                m_device.embedded.push_back(std::move(m_tdevice));
            }
            m_tdevice = UPnPDeviceDesc();
        } else if (!strcmp(name, "controlURL")) {
            m_tservice.controlURL = m_chardata;
        } else if (!strcmp(name, "eventSubURL")) {
            m_tservice.eventSubURL = m_chardata;
        } else if (!strcmp(name, "serviceType")) {
            m_tservice.serviceType = m_chardata;
        } else if (!strcmp(name, "serviceId")) {
            m_tservice.serviceId = m_chardata;
        } else if (!strcmp(name, "SCPDURL")) {
            m_tservice.SCPDURL = m_chardata;
        } else if (!strcmp(name, "deviceType")) {
            dev->deviceType = m_chardata;
        } else if (!strcmp(name, "friendlyName")) {
            dev->friendlyName = m_chardata;
        } else if (!strcmp(name, "manufacturer")) {
            dev->manufacturer = m_chardata;
        } else if (!strcmp(name, "modelName")) {
            dev->modelName = m_chardata;
        } else if (!strcmp(name, "UDN")) {
            dev->UDN = m_chardata;
        } else if (!strcmp(name, "URLBase")) {
            m_device.URLBase = m_chardata;
        }

        m_chardata.clear();
    }

    void CharacterData(const XML_Char *s, int len) override {
        if (s == nullptr || *s == 0)
            return;
        m_chardata.append(s, len);
    }

private:
    UPnPDeviceDesc& m_device;
    string m_chardata;
    UPnPServiceDesc m_tservice;
    UPnPDeviceDesc m_tdevice;
};

static string baseurl(const string& url)
{
    string::size_type pos = url.find("://");
    if (pos == string::npos)
        return url;

    pos = url.find_first_of('/', pos + 3);
    if (pos == string::npos) {
        return url;
    }

    return url.substr(0, pos);
}

UPnPDeviceDesc::UPnPDeviceDesc(const string& url, const string& description)
    : XMLText(description)
{
    //cerr << "UPnPDeviceDesc::UPnPDeviceDesc: url: " << url << endl;
    //cerr << " description " << endl << description << endl;

    UPnPDeviceParser mparser(description, *this);
    if (!mparser.Parse())
        return;
    descURL = url;
    if (URLBase.empty()) {
        // The standard says that if the URLBase value is empty, we should use the url the
        // description was retrieved from. However this is sometimes something like
        // http://host/desc.xml, sometimes something like http://host/ (rare, but e.g. sent by the
        // server on a dlink nas).
        // Also this is wrong because, to be useful, URLBase assumes that relative URLs (e.g. for
        // the service descriptions) will be absolute paths. They could be relative paths instead, in
        // which case descURL (added at some point) should be used and URLBase is useless.
        URLBase = baseurl(url);
    }
    for (auto& dev: embedded) {
        dev.URLBase = URLBase;
    }

    ok = true;
}
