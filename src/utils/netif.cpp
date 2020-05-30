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


namespace NetIF {

class IPAddr::Internal {
public:
    bool ok{false};
    struct sockaddr_storage address;
    struct sockaddr *saddr{nullptr};
};

IPAddr::IPAddr()
{
    m = new Internal;
    m->saddr = reinterpret_cast<struct sockaddr*>(&m->address);
}

IPAddr::IPAddr(const IPAddr& o)
{
    m = new Internal;
    *m = *(o.m);
    m->saddr = reinterpret_cast<struct sockaddr*>(&m->address);
}

IPAddr& IPAddr::operator=(const IPAddr& o)
{
    if (&o != this) {
        delete m;
        m = new Internal;
        *m = *(o.m);
        m->saddr = reinterpret_cast<struct sockaddr*>(&m->address);
    }
    return *this;
}

IPAddr::IPAddr(const char *caddr)
    : IPAddr()
{
    std::memset(&m->address, 0, sizeof(m->address));
    
    if (std::strchr(caddr, ':') != nullptr) {
        if (inet_pton(
                AF_INET6, caddr,
                &reinterpret_cast<struct sockaddr_in6*>(m->saddr)->sin6_addr)
            == 1) {
            m->saddr->sa_family = AF_INET6;
            m->ok = 1;
        }
    } else {
        if (inet_pton(AF_INET, caddr,
                      &reinterpret_cast<struct sockaddr_in*>(m->saddr)->sin_addr)
            == 1) {
            m->saddr->sa_family = AF_INET;
            m->ok = 1;
        }
    }
}

IPAddr::IPAddr(const struct sockaddr *sa)
    : IPAddr()
{
    memset(&m->address, 0, sizeof(m->address));
    switch (sa->sa_family) {
    case AF_INET:
        memcpy(m->saddr, sa, sizeof(struct sockaddr_in));
        m->ok = true;
        break;
    case AF_INET6:
        memcpy(m->saddr, sa, sizeof(struct sockaddr_in6));
        m->ok = true;
        break;
    default:
        break;
    }
}

IPAddr::~IPAddr()
{
    delete m;
}

bool IPAddr::ok() const
{
    return m->ok;
}

bool IPAddr::copyToStorage(struct sockaddr_storage *dest) const
{
    if (!m->ok) {
        memset(dest, 0, sizeof(struct sockaddr_storage));
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
    } else {
        return Family::Invalid;
    }
}

std::string IPAddr::straddr() const
{
    if (!ok())
        return std::string();
    
    char buf[200];
    buf[0] = 0;
    switch(m->saddr->sa_family) {
    case AF_INET:
        inet_ntop(m->saddr->sa_family,
                  &reinterpret_cast<struct sockaddr_in*>(m->saddr)->sin_addr,
                  buf, 200);
    break;
    case AF_INET6:
        inet_ntop(m->saddr->sa_family,
                  &reinterpret_cast<struct sockaddr_in6*>(m->saddr)->sin6_addr,
                  buf, 200);
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
    m = new Internal;
}
Interface::Interface(const Interface& o)
{
    m = new Internal;
    *m = *(o.m);
}
Interface& Interface::operator=(const Interface& o)
{
    if (&o != this) {
        delete m;
        m = new Internal;
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
Interface::~Interface()
{
    delete m;
}

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

const std::pair<const std::vector<IPAddr>&, const std::vector<IPAddr>&>
Interface::getaddresses() const
{
    return std::pair<const std::vector<IPAddr>&, const std::vector<IPAddr>&>
        (m->addresses, m->netmasks);
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
    return m->addresses.empty() ? false : true;
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
        if (entry.family() == IPAddr::Family::IPV6) {
            if (scope == IPAddr::Scope::LINK) {
                if (!IN6_IS_ADDR_LINKLOCAL(
                        &((struct sockaddr_in6 *)(entry.m->saddr))->sin6_addr))    {
                    continue;
                }
            }
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
        flgs.push_back("HASIPV4");
    if (m->flags & static_cast<unsigned int>(Flags::HASIPV6))
        flgs.push_back("HASIPV6");
    if (m->flags & static_cast<unsigned int>(Flags::LOOPBACK))
        flgs.push_back("LOOPBACK");
    if (m->flags & static_cast<unsigned int>(Flags::UP))
        flgs.push_back("UP");
    if (m->flags & static_cast<unsigned int>(Flags::MULTICAST))
        flgs.push_back("MULTICAST");
    if (m->flags & static_cast<unsigned int>(Flags::HASHWADDR))
        flgs.push_back("HASHWADDR");
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
    ~Internal();
    std::vector<Interface> interfaces;
};


#ifndef _WIN32

Interfaces::Internal::Internal()
{
    struct ifaddrs *ifap, *ifa;

    /* Get system interface addresses. */
    if (getifaddrs(&ifap) != 0) {
        std::cerr << "getifaddrs failed\n";
        return;
    }
    std::vector<Interface> vifs;
    for (ifa = ifap; ifa != nullptr; ifa = ifa->ifa_next) {
        // std::cerr << "Interface name " << ifa->ifa_name << "\n";

        // Skip interfaces which are address-less, LOOPBACK, DOWN, or
        // that don't support MULTICAST.
        if (nullptr == ifa->ifa_addr) {
            //std::cerr << "Skipping " << ifa->ifa_name <<
            //    " because noaddr.\n";
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
            ifit->m->setflag(Interface::Flags::LOOPBACK);
        }
        if (ifa->ifa_flags & IFF_UP) {
            ifit->m->setflag(Interface::Flags::UP);
        }
        if (ifa->ifa_flags & IFF_MULTICAST) {
            ifit->m->setflag(Interface::Flags::MULTICAST);
        }
        ifit->m->index = if_nametoindex(ifa->ifa_name);
        switch (ifa->ifa_addr->sa_family) {
        case AF_INET:
        case AF_INET6:
        {
            ifit->m->addresses.emplace_back(ifa->ifa_addr);
            ifit->m->netmasks.emplace_back(ifa->ifa_netmask);
            if (ifa->ifa_addr->sa_family == AF_INET6) {
                ifit->m->setflag(Interface::Flags::HASIPV6);
            } else {
                ifit->m->setflag(Interface::Flags::HASIPV4);
            }
        }
        break;
#ifdef __linux__
        case AF_PACKET:
        {
            auto sll = reinterpret_cast<struct sockaddr_ll*>(ifa->ifa_addr);
            ifit->m->sethwaddr((const char*)sll->sll_addr, sll->sll_halen);
        }
        break;
#else
        case AF_LINK:
        {
            auto sdl = reinterpret_cast<struct sockaddr_dl*>(ifa->ifa_addr);
            ifit->m->sethwaddr((const char*)LLADDR(sdl), sdl->sdl_alen);
        }
        break;
#endif
        default:
            // common: AF_PACKET: 17
            //std::cerr << "Unknown family " << ifa->ifa_addr->sa_family << "\n";
            break;
        }
    }
    interfaces.swap(vifs);
    freeifaddrs(ifap);
}

#else /* _WIN32 ->*/

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

    /* Get Adapters addresses required size. */
    ret = GetAdaptersAddresses(
        AF_UNSPEC,  GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_DNS_SERVER,
        NULL, adapts, &adapts_sz);
    if (ret != ERROR_BUFFER_OVERFLOW) {
        std::cerr << "GetAdaptersAddresses: fail1\n";
        return;
    }
    /* Allocate enough memory. */
    adapts = (PIP_ADAPTER_ADDRESSES) malloc(adapts_sz);
    if (nullptr == adapts) {
        std::cerr << "Out of memory\n";
        return;
    }
    /* Do the call that will actually return the info. */
    ret = GetAdaptersAddresses(
        AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_DNS_SERVER,
        NULL, adapts, &adapts_sz);
    if (ret != ERROR_SUCCESS) {
        std::cerr << "GetAdaptersAddresses: fail2\n";
        goto out;
    }

    adapts_item = adapts;
    while (nullptr != adapts_item) {
        auto ifit = find_if(vifs.begin(), vifs.end(),
                            [adapts_item] (const Interface& ifr) {
                                return adapts_item->AdapterName == ifr.m->name;});
        if (ifit == vifs.end()) {
            vifs.emplace_back(adapts_item->AdapterName);
            ifit = --vifs.end();
        }

        /* We're converting chars using the current code page. It would be nicer
           to convert to UTF-8 */
        char tmpnm[256];
               wcstombs(tmpnm, adapts_item->FriendlyName, sizeof(tmpnm));
        ifit->m->friendlyname = tmpnm;
        if ((adapts_item->Flags & IP_ADAPTER_NO_MULTICAST)) {
            ifit->m->setflag(Interface::Flags::MULTICAST);
        }
        if (adapts_item->OperStatus == IfOperStatusUp) {
            ifit->m->setflag(Interface::Flags::UP);
        }
        if (adapts_item->IfType == IF_TYPE_SOFTWARE_LOOPBACK) {
            ifit->m->setflag(Interface::Flags::LOOPBACK);
        }
        // Note: upnpapi.c used IfIndex instead. In any case the MS
        // doc states that the values are not persistent, so I don't
        // know what this is good for.
        ifit->m->index = adapts_item->Ipv6IfIndex;
        /* The MAC is in the pAdapter->Address char array */
        ifit->m->sethwaddr((const char *)adapts_item->PhysicalAddress,
                           adapts_item->PhysicalAddressLength);
        uni_addr = adapts_item->FirstUnicastAddress;
        while (uni_addr) {
            SOCKADDR *ip_addr = 
                reinterpret_cast<SOCKADDR*>(uni_addr->Address.lpSockaddr);
            switch (ip_addr->sa_family) {
            case AF_INET:
            {
                ifit->m->setflag(Interface::Flags::HASIPV4);
                ifit->m->addresses.emplace_back(ip_addr);
                uint32_t mask =
                    netprefixlentomask(uni_addr->OnLinkPrefixLength);
                struct sockaddr_in sa;
                memset(&sa, 0, sizeof(sa));
                sa.sin_addr.s_addr = mask;
                ifit->m->netmasks.emplace_back((struct sockaddr*)&sa);
            }
            break;
            case AF_INET6:
                ifit->m->setflag(Interface::Flags::HASIPV6);
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

Interfaces::Internal::~Internal()
{
}


Interfaces::Interfaces()
{
    m = new Internal();
}

Interfaces::~Interfaces()
{
    delete m;
}

bool Interfaces::refresh()
{
    delete m;
    m = new Internal();
    return true;
}

Interfaces *theInterfacesP;

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
    }
    return out;
}

std::vector<Interface> Interfaces::select(const Filter& filt) const
{
    unsigned int yesflags{0};
    for (auto f : filt.needs) {
        yesflags |= static_cast<unsigned int>(f);
    }
    unsigned int noflags{0};
    for (auto f : filt.rejects) {
        noflags |= static_cast<unsigned int>(f);
    }
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
    if (m->interfaces.empty()) {
        return nullptr;
    }
    auto it = std::find_if(
        m->interfaces.begin(), m->interfaces.end(),
        [nm] (const Interface& ifr) {
            return nm == ifr.m->name || nm == ifr.m->friendlyname;});

    return it == m->interfaces.end() ? nullptr : &(*it);
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
                uint32_t addr = reinterpret_cast<struct sockaddr_in*>(
                    &sbuf)->sin_addr.s_addr;
                uint32_t mask = reinterpret_cast<struct sockaddr_in*>(
                    &mbuf)->sin_addr.s_addr;
                if ((peeraddr & mask) == (addr & mask)) {
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
        struct sockaddr_in6 *peeraddr =
            reinterpret_cast<struct sockaddr_in6*>(&peerbuf);
        if (IN6_IS_ADDR_V4MAPPED(&peeraddr->sin6_addr)) {
            uint32_t addr4;
            memcpy(&addr4, &peeraddr->sin6_addr.s6_addr[12], 4);
            return interfaceForAddress4(addr4, vifs, hostaddr);
        } else {
            int index = -1;
            if (peeraddr->sin6_scope_id > 0) {
                index = static_cast<int>(peeraddr->sin6_scope_id);
            }

            const Interface *netifp{nullptr};
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
    }
    return nullptr;
}

const Interface* Interfaces::interfaceForAddress(const IPAddr& addr,
                                                 IPAddr& hostaddr)
{
    return interfaceForAddress(addr, m->interfaces, hostaddr);
}

} /* namespace NetIF */
