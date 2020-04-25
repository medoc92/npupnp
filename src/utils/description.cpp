#include "upnpdescription.h"

#include <algorithm>

#include <string.h>

#include "expatmm.hxx"
#include "smallut.h"

using namespace std;

class UPnPDeviceParser : public inputRefXMLParser {
public:
    UPnPDeviceParser(const string& input, UPnPDeviceDesc& device)
        : inputRefXMLParser(input), m_device(device) {}

protected:
    virtual void EndElement(const XML_Char *name) {
        trimstring(m_chardata, " \t\n\r");

        UPnPDeviceDesc *dev;
        bool ismain = false;
        // Arghh: upmpdcli wrongly used devicelist instead of
        // deviceList. Support both as it is unlikely that anybody
        // would use both for different purposes
        if (find_if(m_path.begin(), m_path.end(),
					[] (const StackEl& el) {
						return !stringlowercmp("devicelist", el.name);})
			== m_path.end()) {
            dev = &m_device;
            ismain = true;
        } else {
            dev = &m_tdevice;
            ismain = false;
        }

        if (!strcmp(name, "service")) {
            dev->services.push_back(m_tservice);
            m_tservice = UPnPServiceDesc();
        } else if (!strcmp(name, "device")) {
            if (!ismain) {
                m_device.embedded.push_back(m_tdevice);
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

    virtual void CharacterData(const XML_Char *s, int len) {
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
    if (URLBase.empty()) {
        // The standard says that if the URLBase value is empty, we
        // should use the url the description was retrieved
        // from. However this is sometimes something like
        // http://host/desc.xml, sometimes something like http://host/
        // (rare, but e.g. sent by the server on a dlink nas).
        URLBase = baseurl(url);
    }
    for (auto& dev: embedded) {
        dev.URLBase = URLBase;
    }

    ok = true;
}
