/*******************************************************************************
 *
 * Copyright (c) 2020 Jean-Francois Dockes
 *
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions are met: 
 *
 * - Redistributions of source code must retain the above copyright notice, 
 * this list of conditions and the following disclaimer. 
 * - Redistributions in binary form must reproduce the above copyright notice, 
 * this list of conditions and the following disclaimer in the documentation 
 * and/or other materials provided with the distribution. 
 * - Neither name of Intel Corporation nor the names of its contributors 
 * may be used to endorse or promote products derived from this software 
 * without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS 
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT 
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR 
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL INTEL OR 
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, 
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, 
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR 
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY 
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS 
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 ******************************************************************************/
#include "netif.h"

#include <cstring>
#include <ostream>
#include <vector>
#include <iostream>
#include <algorithm>
#include <numeric>
#include <sstream>

#ifndef _WIN32

#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <net/if.h>
#include <ifaddrs.h>
#ifdef __linux__
#include <netpacket/packet.h>
#else
#include <net/if_dl.h>
#endif

#else /* _WIN32 -> */
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0600

#include <winsock2.h>
#include <iphlpapi.h>
#include <ws2tcpip.h>

#ifndef __MINGW32__
#include "inet_pton.h"
#endif

#endif /* _WIN32 */


#ifndef IN6_IS_ADDR_GLOBAL
#define IN6_IS_ADDR_GLOBAL(a) \
    ((((__const uint32_t *) (a))[0] & htonl(0x70000000)) == htonl (0x20000000))
#endif /* IN6_IS_ADDR_GLOBAL */

#ifndef IN6_IS_ADDR_ULA
#define IN6_IS_ADDR_ULA(a) \
        ((((__const uint32_t *) (a))[0] & htonl(0xfe000000)) == htonl (0xfc000000))
#endif /* IN6_IS_ADDR_ULA */


namespace NetIF {

static FILE *logfp;

// #define NETIF_DEBUG

#define LOGERR(X) do {                                              \
        if (logfp) {                                                \
            std::ostringstream oss;                                 \
            oss << X;                                               \
            fprintf(logfp, "%s", oss.str().c_str());                \
        }                                                           \
    } while (0)
#ifdef NETIF_DEBUG
#define LOGDEB(X) LOGERR(X)
#else
#define LOGDEB(X)
#endif

class IPAddr::Internal {
public:
    bool ok{false};
    struct sockaddr_storage address{};
    struct sockaddr *saddr{nullptr};
};

IPAddr::IPAddr()
{
    m = std::make_unique<Internal>();
    m->saddr = reinterpret_cast<struct sockaddr*>(&m->address);
}

IPAddr::IPAddr(const IPAddr& o)
{
    m = std::make_unique<Internal>();
    *m = *(o.m);
    m->saddr = reinterpret_cast<struct sockaddr*>(&m->address);
}

IPAddr& IPAddr::operator=(const IPAddr& o)
{
    if (&o != this) {
        m = std::make_unique<Internal>();
        *m = *(o.m);
        m->saddr = reinterpret_cast<struct sockaddr*>(&m->address);
    }
    return *this;
}

IPAddr& IPAddr::operator=(IPAddr&& o)
{
    if (&o != this) {
        m = std::make_unique<Internal>();
        *m = std::move(*(o.m));
        m->saddr = reinterpret_cast<struct sockaddr*>(&m->address);
    }
    return *this;
}

IPAddr::IPAddr(const char *caddr)
    : IPAddr()
{
    if (nullptr != std::strchr(caddr, ':')) {
        if (inet_pton(AF_INET6, caddr,
                      &reinterpret_cast<struct sockaddr_in6*>(m->saddr)->sin6_addr) == 1) {
            m->saddr->sa_family = AF_INET6;
            m->ok = true;
        }
    } else {
        if (inet_pton(AF_INET, caddr,
                      &reinterpret_cast<struct sockaddr_in*>(m->saddr)->sin_addr) == 1) {
            m->saddr->sa_family = AF_INET;
            m->ok = true;
        }
    }
}

static const uint8_t ipv4mappedprefix[12] = {0,0,0,0,0,0,0,0,0,0,0xff,0xff};

IPAddr::IPAddr(const struct sockaddr *sa, bool unmapv4)
    : IPAddr()
{
    switch (sa->sa_family) {
    case AF_INET:
        memcpy(m->saddr, sa, sizeof(struct sockaddr_in));
        m->ok = true;
        break;
    case AF_INET6:
    {
        if (unmapv4) {
            const uint8_t *bytes =
                reinterpret_cast<const struct sockaddr_in6 *>(sa)->sin6_addr.s6_addr;
            if (!memcmp(bytes, ipv4mappedprefix, 12)) {
                auto a = reinterpret_cast<struct sockaddr_in*>(m->saddr);
                memset(a, 0, sizeof(*a));
                a->sin_family = AF_INET;
                memcpy(&a->sin_addr.s_addr, bytes+12, 4);
                m->ok = true;
                break;
            }
        }
        // unmapv4==false or not a v4 mapped address, copy v6 address
        memcpy(m->saddr, sa, sizeof(struct sockaddr_in6));
        m->ok = true;
    }
    break;
    default:
        break;
    }
}

IPAddr::~IPAddr() = default;

bool IPAddr::ok() const
{
    return m->ok;
}

bool IPAddr::copyToStorage(struct sockaddr_storage *dest) const
{
    if (!m->ok) {
        *dest = {};
        return false;
    }
    memcpy(dest, &m->address, sizeof(struct sockaddr_storage));
    return true;
}

bool IPAddr::copyToAddr(struct sockaddr *dest) const
{
    if (!m->ok) {
        return false;
    }
    switch (m->saddr->sa_family) {
    case AF_INET:
        memcpy(dest, m->saddr, sizeof(struct sockaddr_in));
        break;
    case AF_INET6:
        memcpy(dest, m->saddr, sizeof(struct sockaddr_in6));
        break;
    default:
        return false;
    }
    return true;
}

const struct sockaddr_storage& IPAddr::getaddr() const
{
    return m->address;
}

IPAddr::Family IPAddr::family() const
{
    if (m->ok) {
        return static_cast<IPAddr::Family>(m->saddr->sa_family);
    }
    return Family::Invalid;
}

IPAddr::Scope IPAddr::scopetype() const
{
    if (!m->ok)
        return Scope::Invalid;
    if (family() != Family::IPV6)
        return Scope::Invalid;

    // Unrouted link-local address. If there are several interfaces on
    // the host, an additional piece of information (scope id) is
    // necessary to determine what interface they belong too.
    // e.g. fe80::1 could exist on both eth0 and eth1, and needs
    // scopeid 0/1 for complete determination
    if (IN6_IS_ADDR_LINKLOCAL(
            &(reinterpret_cast<struct sockaddr_in6*>(m->saddr))->sin6_addr)) {
        return Scope::LINK;
    }

    // Site-local addresses are deprecated. Prefix fec0, then locally
    // chosen network and interface ids. Routable within the
    // site. They also need a site/scope ID, always 1 if there is only
    // one site defined.
    if (IN6_IS_ADDR_SITELOCAL(
            &(reinterpret_cast<struct sockaddr_in6*>(m->saddr))->sin6_addr)) {
        return Scope::SITE;
    }

    // We process unique local addresses and global ones in the same way.
    return Scope::GLOBAL;
}

bool IPAddr::setScopeIdx(const IPAddr& other)
{
    if (family() != Family::IPV6 || other.family() != Family::IPV6 ||
        scopetype() != Scope::LINK || other.scopetype() != Scope::LINK) {
        return false;
    }
    auto msa6 = reinterpret_cast<struct sockaddr_in6*>(m->saddr);
    auto osa6 = reinterpret_cast<struct sockaddr_in6*>(other.m->saddr);
    msa6->sin6_scope_id = osa6->sin6_scope_id;
    return true;
}

std::string IPAddr::straddr() const
{
    return straddr(false, false);
}

std::string IPAddr::straddr(bool setscope, bool forurl) const
{
    if (!ok())
        return {};
    
    char buf[200];
    buf[0] = 0;
    switch(m->saddr->sa_family) {
    case AF_INET:
        inet_ntop(m->saddr->sa_family,
                  &reinterpret_cast<struct sockaddr_in*>(m->saddr)->sin_addr, buf, 200);
    break;
    case AF_INET6:
    {
        auto sa6 = reinterpret_cast<struct sockaddr_in6*>(m->saddr);
        inet_ntop(m->saddr->sa_family, &sa6->sin6_addr, buf, 200);
        if (!setscope || scopetype() != Scope::LINK) {
            return buf;
        }
        std::string s{buf};
        char scopebuf[30];
        snprintf(scopebuf, sizeof(scopebuf), "%u", sa6->sin6_scope_id);
        s += std::string(forurl ? "%25" : "%") + scopebuf;
        return s;
    }
    break;
    }
    return buf;
}

class Interface::Internal {
public:
    unsigned int flags{0};
    std::string name;
    std::string friendlyname;
    int index{-1};
    std::string hwaddr;
    std::vector<IPAddr> addresses;
    std::vector<IPAddr> netmasks;
    void setflag(Interface::Flags f);
    void sethwaddr(const char *addr, int len) {
        bool isnull{true};
        for (int i = 0; i < len; i++) {
            if (addr[i] != 0) {
                isnull = false;
                break;
            }
        }
        if (isnull)
            return;
        hwaddr.assign(addr, len);
        flags |= static_cast<unsigned int>(Interface::Flags::HASHWADDR);
    }
};

Interface::Interface()
{
    m = std::make_unique<Internal>();
}
Interface::Interface(const Interface& o)
{
    m = std::make_unique<Internal>();
    *m = *(o.m);
}
Interface& Interface::operator=(const Interface& o)
{
    if (&o != this) {
        m = std::make_unique<Internal>();
        *m = *(o.m);
    }
    return *this;
}

Interface::Interface(const char *nm)
    : Interface()
{
    m->name = nm;
}
Interface::Interface(const std::string& nm)
    : Interface()
{
    m->name = nm;
}
Interface::~Interface() = default;

void Interface::Internal::setflag(Interface::Flags f)
{
    flags |= static_cast<unsigned int>(f);
}

bool Interface::hasflag(Flags f) const
{
    return (m->flags & static_cast<unsigned int>(f)) != 0;
}

const std::string& Interface::gethwaddr() const
{
    return m->hwaddr;
}

std::string Interface::gethexhwaddr() const
{
    char buf[20];
    snprintf(buf, 20, "%02x:%02x:%02x:%02x:%02x:%02x",
             m->hwaddr[0]&0xFF, m->hwaddr[1]&0xFF, m->hwaddr[2]&0xFF,
             m->hwaddr[3]&0xFF, m->hwaddr[4]&0xFF, m->hwaddr[5]&0xFF);
    return buf;
}

int Interface::getindex() const
{
    return m->index;
}

const std::string& Interface::getname() const
{
    return m->name;
}

const std::string& Interface::getfriendlyname() const
{
    return m->friendlyname.empty() ? m->name : m->friendlyname;
}

std::pair<const std::vector<IPAddr>&, const std::vector<IPAddr>&>
Interface::getaddresses() const {
    return {m->addresses, m->netmasks};
}

bool Interface::trimto(const std::vector<IPAddr>& keep)
{
    auto mit = m->netmasks.begin();
    for (auto ait = m->addresses.begin(); ait != m->addresses.end();) {
        auto it = find_if(keep.begin(), keep.end(),
                          [ait] (const IPAddr& a) {
                              return ait->straddr() == a.straddr();
                          });
        if (it == keep.end()) {
            ait = m->addresses.erase(ait);
            mit = m->netmasks.erase(mit);
        } else {
            ait++;
            mit++;
        }
    }
    return !m->addresses.empty();
}

const IPAddr *Interface::firstipv4addr() const
{
    if (!hasflag(Flags::HASIPV4)) {
        return nullptr;
    }
    for (const auto& entry: m->addresses) {
        if (entry.family() == IPAddr::Family::IPV4) {
            return &entry;
        }
    }
    return nullptr;
}

const IPAddr *Interface::firstipv6addr(IPAddr::Scope scope) const
{
    if (!hasflag(Flags::HASIPV6)) {
        return nullptr;
    }
    for (const auto& entry: m->addresses) {
        if (entry.family() == IPAddr::Family::IPV6 &&
            (scope != IPAddr::Scope::LINK ||
             IN6_IS_ADDR_LINKLOCAL(
                 &(reinterpret_cast<struct sockaddr_in6*>(entry.m->saddr))->sin6_addr))) {
            return &entry;
        }
    }
    return nullptr;
}

std::ostream& Interface::print(std::ostream& out) const
{
    out << m->name << ": <";
    std::vector<std::string> flgs;
    if (m->flags & static_cast<unsigned int>(Flags::HASIPV4))
        flgs.emplace_back("HASIPV4");
    if (m->flags & static_cast<unsigned int>(Flags::HASIPV6))
        flgs.emplace_back("HASIPV6");
    if (m->flags & static_cast<unsigned int>(Flags::LOOPBACK))
        flgs.emplace_back("LOOPBACK");
    if (m->flags & static_cast<unsigned int>(Flags::UP))
        flgs.emplace_back("UP");
    if (m->flags & static_cast<unsigned int>(Flags::MULTICAST))
        flgs.emplace_back("MULTICAST");
    if (m->flags & static_cast<unsigned int>(Flags::HASHWADDR))
        flgs.emplace_back("HASHWADDR");
    auto it = flgs.begin();
    if (it != flgs.end())
        out << *it++;
    while (it != flgs.end())
        out << "|" << *it++;
    out << ">\n";
    if (!m->hwaddr.empty()) {
        out << "hwaddr " << gethexhwaddr() << "\n";
    }
    for (unsigned int i = 0; i < m->addresses.size(); i++) {
        out << m->addresses[i].straddr() << " " <<
            m->netmasks[i].straddr() <<"\n";
    }
    return out;
}

class Interfaces::Internal {
public:
    Internal();
    std::vector<Interface> interfaces;
};


#ifndef _WIN32

Interfaces::Internal::Internal()
{
    struct ifaddrs *ifap, *ifa;

    /* Get system interface addresses. */
    if (getifaddrs(&ifap) != 0) {
        LOGERR("NetIF::Interfaces: getifaddrs failed\n");
        return;
    }
    std::vector<Interface> vifs;
    for (ifa = ifap; ifa != nullptr; ifa = ifa->ifa_next) {
        LOGDEB("NetIF::Interfaces: I/F name " << ifa->ifa_name << "\n");
        // Skip interfaces which are address-less.
        if (nullptr == ifa->ifa_addr) {
            LOGDEB("NetIF::Interfaces: Skipping " << ifa->ifa_name <<
                   " because it has no address.\n");
            continue;
        }
        auto ifit = find_if(vifs.begin(), vifs.end(),
                          [ifa] (const Interface& ifr) {
                              return ifa->ifa_name == ifr.m->name;});
        if (ifit == vifs.end()) {
            vifs.emplace_back(ifa->ifa_name);
            ifit = --vifs.end();
        }
        if (ifa->ifa_flags & IFF_LOOPBACK) {
            LOGDEB("NetIF::Interfaces: " << ifa->ifa_name << " is loopback\n");
            ifit->m->setflag(Interface::Flags::LOOPBACK);
        }
        if (ifa->ifa_flags & IFF_UP) {
            LOGDEB("NetIF::Interfaces: " << ifa->ifa_name << " is UP\n");
            ifit->m->setflag(Interface::Flags::UP);
        }
        if (ifa->ifa_flags & IFF_MULTICAST) {
            LOGDEB("NetIF::Interfaces: " << ifa->ifa_name << " has MULTICAST\n");
            ifit->m->setflag(Interface::Flags::MULTICAST);
        }
#ifdef __HAIKU__
        // It seems that Haiku does not set the MULTICAST flag even on
        // interfaces which do support the function. So, force it and hope for the
        // best:
        ifit->m->setflag(Interface::Flags::MULTICAST);
#endif        
        ifit->m->index = if_nametoindex(ifa->ifa_name);
        switch (ifa->ifa_addr->sa_family) {
        case AF_INET:
        case AF_INET6:
        {
            ifit->m->addresses.emplace_back(ifa->ifa_addr);
            ifit->m->netmasks.emplace_back(ifa->ifa_netmask);
            if (ifa->ifa_addr->sa_family == AF_INET6) {
                LOGDEB("NetIF::Interfaces: " << ifa->ifa_name << " has IPV6\n");
                ifit->m->setflag(Interface::Flags::HASIPV6);
            } else {
                LOGDEB("NetIF::Interfaces: " << ifa->ifa_name << " has IPV4\n");
                ifit->m->setflag(Interface::Flags::HASIPV4);
            }
        }
        break;
#ifdef __linux__
        case AF_PACKET:
        {
            auto sll = reinterpret_cast<struct sockaddr_ll*>(ifa->ifa_addr);
            ifit->m->sethwaddr(reinterpret_cast<const char*>(sll->sll_addr), sll->sll_halen);
        }
        break;
#else
        case AF_LINK:
        {
            auto sdl = reinterpret_cast<struct sockaddr_dl*>(ifa->ifa_addr);
            LOGDEB("NetIF::Interfaces: " << ifa->ifa_name << " has hwaddr\n");
            ifit->m->sethwaddr((const char*)LLADDR(sdl), sdl->sdl_alen);
        }
        break;
#endif
        default:
            // common: AF_PACKET: 17
            LOGDEB("NetIF::Interfaces: " << ifa->ifa_name <<
                   "Unknown family " << ifa->ifa_addr->sa_family << "\n");
            break;
        }
    }
    interfaces.swap(vifs);
    freeifaddrs(ifap);
}

#else /* _WIN32 ->*/

bool wchartoutf8(const wchar_t *in, std::string& out, size_t wlen)
{
    out.clear();
    if (nullptr == in) {
        return true;
    }
    if (wlen == 0) {
        wlen = wcslen(in);
    }
    int flags = WC_ERR_INVALID_CHARS;
    int bytes = ::WideCharToMultiByte(CP_UTF8, flags, in, wlen, nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) {
        LOGERR("wchartoutf8: conversion error1\n");
        fwprintf(stderr, L"wchartoutf8: conversion error1 for [%s]\n", in);
        return false;
    }
    char *cp = (char *)malloc(bytes+1);
    if (nullptr == cp) {
        LOGERR("wchartoutf8: malloc failed\n");
        return false;
    }
    bytes = ::WideCharToMultiByte(CP_UTF8, flags, in, wlen, cp, bytes, nullptr, nullptr);
    if (bytes <= 0) {
        LOGERR("wchartoutf8: CONVERSION ERROR2\n");
        free(cp);
        return false;
    }
    cp[bytes] = 0;
    out = cp;
    free(cp);
    //fwprintf(stderr, L"wchartoutf8: in: [%s]\n", in);
    //fprintf(stderr, "wchartoutf8: out:  [%s]\n", out.c_str());
    return true;
}

static uint32_t netprefixlentomask(uint8_t pfxlen)
{
    uint32_t out{0};
    pfxlen = std::min(pfxlen, uint8_t(31));
    for (int i = 0; i < pfxlen; i++) {
        out |= 1 << (31-i);
    }
    return out;
}

Interfaces::Internal::Internal()
{
    PIP_ADAPTER_ADDRESSES adapts{nullptr};
    PIP_ADAPTER_ADDRESSES adapts_item;
    PIP_ADAPTER_UNICAST_ADDRESS uni_addr;
    ULONG adapts_sz = 0;
    ULONG ret;
    std::vector<Interface> vifs;

    // Note: we are using the GetAdaptersAddresses() call which is the
    // recommended interface for modern Windows. However the old lib
    // (-0.17) used getAdaptersInfo(), which gives different names for
    // the adapters, which means that, e.g. if the adapter name is
    // specified in the upplay prefs, it will be need to be set again
    // after changing versions.
    
    /* Get Adapters addresses required size. */
    ret = GetAdaptersAddresses(
        AF_UNSPEC,  GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_DNS_SERVER, NULL, adapts, &adapts_sz);
    if (ret != ERROR_BUFFER_OVERFLOW) {
        LOGERR("NetIF::Interfaces: GetAdaptersAddresses: ret1 " << ret <<"\n");
        return;
    }
    /* Allocate enough memory. */
    adapts = (PIP_ADAPTER_ADDRESSES) malloc(adapts_sz);
    if (nullptr == adapts) {
        LOGERR("NetIF::Interfaces: GetAdaptersAddresses: Out of memory\n");
        return;
    }
    /* Do the call that will actually return the info. */
    ret = GetAdaptersAddresses(
        AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_DNS_SERVER, NULL, adapts, &adapts_sz);
    if (ret != ERROR_SUCCESS) {
        LOGERR("NetIF::Interfaces: GetAdaptersAddresses: ret2 " << ret <<"\n");
        goto out;
    }

    adapts_item = adapts;
    while (nullptr != adapts_item) {
        auto ifit = find_if(vifs.begin(), vifs.end(),
                            [adapts_item] (const Interface& ifr) {
                                return adapts_item->AdapterName == ifr.m->name;});
        if (ifit == vifs.end()) {
            LOGDEB("NetIF::Interfaces: I/F friendlyname " << tmpnm <<
                   " AdapterName " << adapts_item->AdapterName << "\n");
            vifs.emplace_back(adapts_item->AdapterName);
            ifit = --vifs.end();
        }
        if (!wchartoutf8(adapts_item->FriendlyName, ifit->m->friendlyname, 0)) {
            ifit->m->friendlyname = adapts_item->AdapterName;
        }
        if (!(adapts_item->Flags & IP_ADAPTER_NO_MULTICAST)) {
            LOGDEB("NetIF::Interfaces: " << tmpnm << " has MULTICAST\n");
            ifit->m->setflag(Interface::Flags::MULTICAST);
        }
        if (adapts_item->OperStatus == IfOperStatusUp) {
            LOGDEB("NetIF::Interfaces: " << tmpnm << " is UP\n");
            ifit->m->setflag(Interface::Flags::UP);
        }
        if (adapts_item->IfType == IF_TYPE_SOFTWARE_LOOPBACK) {
            LOGDEB("NetIF::Interfaces: " << tmpnm << " is LOOPBACK\n");
            ifit->m->setflag(Interface::Flags::LOOPBACK);
        }
        // Note: upnpapi.c used IfIndex instead.
        ifit->m->index = adapts_item->Ipv6IfIndex;
        /* The MAC is in the pAdapter->Address char array */
        ifit->m->sethwaddr((const char *)adapts_item->PhysicalAddress,
                           adapts_item->PhysicalAddressLength);
        LOGDEB("NetIF::Interfaces: " << tmpnm << " has hwaddr\n");
        uni_addr = adapts_item->FirstUnicastAddress;
        while (uni_addr) {
            SOCKADDR *ip_addr = 
                reinterpret_cast<SOCKADDR*>(uni_addr->Address.lpSockaddr);
            switch (ip_addr->sa_family) {
            case AF_INET:
            {
                ifit->m->setflag(Interface::Flags::HASIPV4);
                LOGDEB("NetIF::Interfaces: " << tmpnm << " has IPV4\n");
                ifit->m->addresses.emplace_back(ip_addr);
                uint32_t mask =
                    netprefixlentomask(uni_addr->OnLinkPrefixLength);
                struct sockaddr_in sa = {};
                sa.sin_addr.s_addr = mask;
                ifit->m->netmasks.emplace_back((struct sockaddr*)&sa);
            }
            break;
            case AF_INET6:
                ifit->m->setflag(Interface::Flags::HASIPV6);
                LOGDEB("NetIF::Interfaces: " << tmpnm << " has IPV6\n");
                ifit->m->addresses.emplace_back(ip_addr);
                // Not for now...
                ifit->m->netmasks.emplace_back();
                break;

            default:
                break;
            }
            /* Next address. */
            uni_addr = uni_addr->Next;
        }

        /* Next adapter. */
        adapts_item = adapts_item->Next;
    }
    interfaces.swap(vifs);

out:
    free(adapts);
}

#endif /* _WIN32 */

Interfaces::Interfaces()
{
    m = std::make_unique<Internal>();
}

Interfaces::~Interfaces() = default;

bool Interfaces::refresh()
{
    m = std::make_unique<Internal>();
    return true;
}

void Interfaces::setlogfp(FILE *fp)
{
    logfp = fp;
}

static Interfaces *theInterfacesP;

Interfaces *Interfaces::theInterfaces()
{
    if (nullptr == theInterfacesP) {
        theInterfacesP = new Interfaces();
    }
    return theInterfacesP;
}

std::ostream& Interfaces::print(std::ostream& out) {
    const auto& ifs = theInterfaces()->m->interfaces;
    for (const auto& entry : ifs) {
        entry.print(out);
        out << "\n";
    }
    return out;
}

std::vector<Interface> Interfaces::select(const Filter& filt) const
{
    uint32_t yesflags = std::accumulate(filt.needs.begin(), filt.needs.end(), 0,
        [](uint32_t yes, const NetIF::Interface::Flags &f){ return yes | static_cast<unsigned int>(f); });

    uint32_t noflags = std::accumulate(filt.rejects.begin(), filt.rejects.end(), 0,
        [](uint32_t no, const NetIF::Interface::Flags &f){ return no | static_cast<unsigned int>(f); });

    LOGDEB("Interfaces::select: yesflags " << std::hex << yesflags <<
           " noflags " << noflags << std::dec << "\n");

    std::vector<Interface> out;
    const auto& ifs = theInterfaces()->m->interfaces;
    std::copy_if(ifs.begin(), ifs.end(), std::back_inserter(out),
        [=](const NetIF::Interface &entry){
                     return (entry.m->flags & yesflags) == yesflags &&
                         (entry.m->flags & noflags) == 0;});
    return out;
}

Interface *Interfaces::findByName(const char *nm) const
{
    for (auto& ifr : m->interfaces)
        if (nm == ifr.m->name || nm == ifr.m->friendlyname)
            return &ifr;

    return nullptr;
}

static const Interface* interfaceForAddress4(
    uint32_t peeraddr, const std::vector<Interface>& vifs, IPAddr& hostaddr)
{
    struct sockaddr_storage sbuf, mbuf;
    for (const auto& netif : vifs) {
        auto addresses = netif.getaddresses();
        for (unsigned int i = 0; i < addresses.first.size(); i++) {
            if (addresses.first[i].family() == IPAddr::Family::IPV4) {
                addresses.first[i].copyToStorage(&sbuf);
                addresses.second[i].copyToStorage(&mbuf);
                uint32_t addr = reinterpret_cast<struct sockaddr_in*>(&sbuf)->sin_addr.s_addr;
                uint32_t mask = reinterpret_cast<struct sockaddr_in*>(&mbuf)->sin_addr.s_addr;
                if (
                    // Special case for having a single interface with a netmask of ffffffff, which
                    // is apparently common from FreeBSD jails. Just return it, there is no way we
                    // can check anything.
                    ((vifs.size() == 1) && (mask == 0xffffffff)) ||
                    // Normal subnet check
                    ((peeraddr & mask) == (addr & mask)) ) {
                    hostaddr = addresses.first[i];
                    return &netif;
                }
            }
        }
    }
    return nullptr;
}
    
const Interface *Interfaces::interfaceForAddress(
    const IPAddr& addr, const std::vector<Interface>& vifs, IPAddr& hostaddr)
{
    struct sockaddr_storage peerbuf;
    addr.copyToStorage(&peerbuf);

    if (addr.family() == IPAddr::Family::IPV4) {
        uint32_t peeraddr = reinterpret_cast<struct sockaddr_in*>(
            &peerbuf)->sin_addr.s_addr;
        return interfaceForAddress4(peeraddr, vifs, hostaddr);
    }

    if (addr.family() == IPAddr::Family::IPV6)    {
        auto peeraddr =
            reinterpret_cast<struct sockaddr_in6*>(&peerbuf);
        if (IN6_IS_ADDR_V4MAPPED(&peeraddr->sin6_addr)) {
            uint32_t addr4;
            memcpy(&addr4, &peeraddr->sin6_addr.s6_addr[12], 4);
            return interfaceForAddress4(addr4, vifs, hostaddr);
        }

        int index = -1;
        if (peeraddr->sin6_scope_id > 0) {
            index = static_cast<int>(peeraddr->sin6_scope_id);
        }

        const Interface* netifp{nullptr};
        for (const auto& netif : vifs) {
            if (netif.hasflag(Interface::Flags::HASIPV6)) {
                if (nullptr == netifp) {
                    netifp = &netif;
                }
                if (netif.getindex() == index) {
                    netifp = &netif;
                }
            }
        }
        hostaddr = IPAddr();
        if (netifp) {
            const auto ipaddr = netifp->firstipv6addr(IPAddr::Scope::LINK);
            if (ipaddr) {
                hostaddr = *ipaddr;
            }
        }
        return netifp;
    }
    return nullptr;
}

const Interface* Interfaces::interfaceForAddress(const IPAddr& addr,
                                                 IPAddr& hostaddr)
{
    return interfaceForAddress(addr, m->interfaces, hostaddr);
}

} /* namespace NetIF */
