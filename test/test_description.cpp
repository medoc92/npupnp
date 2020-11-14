/* Copyright (C) 2016 J.F.Dockes
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Lesser General Public License as published by
 *   the Free Software Foundation; either version 2.1 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public License
 *   along with this program; if not, write to the
 *   Free Software Foundation, Inc.,
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#include "upnpdescription.h"

#include "upnp.h"

#include <string>
#include <iostream>

using namespace std;

static const char* description_text =
    R"(<?xml version="1.0" encoding="utf-8"?>
<root xmlns="urn:schemas-upnp-org:device-1-0">
  <specVersion><major>1</major><minor>1</minor></specVersion>
  <device>
    <deviceType>urn:schemas-upnp-org:device:MediaRenderer:1</deviceType>
    <friendlyName>upmpd-bureau-UPnP/AV</friendlyName>
    <manufacturer>JF Light Industries</manufacturer>
    <manufacturerURL>https://framagit.org/medoc92</manufacturerURL>
    <modelDescription>UPnP front-end to MPD</modelDescription>
    <modelName>UpMPD</modelName>
    <modelNumber>42</modelNumber>
    <modelURL>https://framagit.org/medoc92/upmpdcli/code/</modelURL>
    <serialNumber>upmpdcli version 1.5.0 libupnpp 0.20.0</serialNumber>
    <presentationURL>/uuid-4152bae3-1c33-7bae-6b71-ac220b4f46f0/presentation.html</presentationURL>    
    <UDN>uuid:4152bae3-1c33-7bae-6b71-ac220b4f46f0</UDN>
    <serviceList>
      <service>
        <serviceType>urn:schemas-upnp-org:service:AVTransport:1</serviceType>
        <serviceId>urn:upnp-org:serviceId:AVTransport</serviceId>
        <SCPDURL>/uuid-4152bae3-1c33-7bae-6b71-ac220b4f46f0/urn-schemas-upnp-org-service-AVTransport-1.xml</SCPDURL>
        <controlURL>/uuid-4152bae3-1c33-7bae-6b71-ac220b4f46f0/ctl-urn-schemas-upnp-org-service-AVTransport-1</controlURL>
        <eventSubURL>/uuid-4152bae3-1c33-7bae-6b71-ac220b4f46f0/evt-urn-schemas-upnp-org-service-AVTransport-1</eventSubURL>
      </service>
      <service>
        <serviceType>urn:schemas-upnp-org:service:RenderingControl:1</serviceType>
        <serviceId>urn:upnp-org:serviceId:RenderingControl</serviceId>
        <SCPDURL>/uuid-4152bae3-1c33-7bae-6b71-ac220b4f46f0/urn-schemas-upnp-org-service-RenderingControl-1.xml</SCPDURL>
        <controlURL>/uuid-4152bae3-1c33-7bae-6b71-ac220b4f46f0/ctl-urn-schemas-upnp-org-service-RenderingControl-1</controlURL>
        <eventSubURL>/uuid-4152bae3-1c33-7bae-6b71-ac220b4f46f0/evt-urn-schemas-upnp-org-service-RenderingControl-1</eventSubURL>
      </service>
    </serviceList>
    <deviceList>
      <device>
        <deviceType>urn:schemas-upnp-org:device:MediaServer:1</deviceType>
        <manufacturer>lesbonscomptes.com/upmpdcli</manufacturer>
        <modelName>Upmpdcli Media Server</modelName>
        <friendlyName>upmpd-bureau-mediaserver</friendlyName>
        <UDN>uuid:5efe1e0b-0f36-cfcf-7229-ac220b4f46f0</UDN>
        <serviceList>
          <service>
            <serviceType>urn:schemas-upnp-org:service:ContentDirectory:1</serviceType>
             <serviceId>urn:upnp-org:serviceId:ContentDirectory</serviceId>
             <SCPDURL>/uuid-5efe1e0b-0f36-cfcf-7229-ac220b4f46f0/urn-schemas-upnp-org-service-ContentDirectory-1.xml</SCPDURL>
             <controlURL>/uuid-5efe1e0b-0f36-cfcf-7229-ac220b4f46f0/ctl-urn-schemas-upnp-org-service-ContentDirectory-1</controlURL>
             <eventSubURL>/uuid-5efe1e0b-0f36-cfcf-7229-ac220b4f46f0/evt-urn-schemas-upnp-org-service-ContentDirectory-1</eventSubURL>
           </service>
        </serviceList>
      </device>
      <device>
        <deviceType>urn:schemas-upnp-org:device:SomeDevice:1</deviceType>
        <manufacturer>lesbonscomptes.com/upmpdcli</manufacturer>
        <modelName>Upmpdcli Other Device</modelName>
        <friendlyName>upmpd-bureau-otherdevice</friendlyName>
        <UDN>uuid:5efe1e0b-0f36-cfcf-7229-ac220b4f46f0</UDN>
        <serviceList>
          <service>
            <serviceType>urn:schemas-upnp-org:service:ContentDirectory:1</serviceType>
             <serviceId>urn:upnp-org:serviceId:ContentDirectory</serviceId>
             <SCPDURL>/uuid-5efe1e0b-0f36-cfcf-7229-ac220b4f46f0/urn-schemas-upnp-org-service-ContentDirectory-1.xml</SCPDURL>
             <controlURL>/uuid-5efe1e0b-0f36-cfcf-7229-ac220b4f46f0/ctl-urn-schemas-upnp-org-service-ContentDirectory-1</controlURL>
             <eventSubURL>/uuid-5efe1e0b-0f36-cfcf-7229-ac220b4f46f0/evt-urn-schemas-upnp-org-service-ContentDirectory-1</eventSubURL>
           </service>
        </serviceList>
      </device>
    </deviceList>
  </device>
</root>
)";
    
int main(int argc, char *argv[])
{
    UPnPDeviceDesc desc("http://192.168.1.1/somedir/desc.xml", description_text);
    if (!desc.ok) {
        std::cerr << "Parse failed\n";
        return 1;
    }

    std::cout << "Root device: type: [" << desc.deviceType << "] fname [" <<
        desc.friendlyName << "] model [" << desc.modelName << "]\n";

    for (const auto& dev : desc.embedded) {
        std::cout << "Embedded device: type: [" << dev.deviceType <<
            "] fname [" << dev.friendlyName << "] model [" <<
            dev.modelName << "]\n";
    }
    return 0;
}
