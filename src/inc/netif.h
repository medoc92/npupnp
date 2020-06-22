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
#ifndef _NETIF_H_INCLUDED_
#define _NETIF_H_INCLUDED_

/*
 * Offer a simplified and system-idependant interface to a system's network
 * interfaces.
 */
#include <string>
#include <ostream>
#include <vector>
#ifdef _WIN32
#include <winsock2.h>
#else
#include <netinet/in.h>
#endif

namespace NetIF {

/** Represent an IPV4 or IPV6 address */
class IPAddr {
public:
    enum class Family {Invalid = -1, IPV4 = AF_INET, IPV6 = AF_INET6};
    enum class Scope {Invalid = -1, LINK, SITE, GLOBAL};
    IPAddr();
    /** Build from textual representation (e.g. 192.168.4.4) */
    explicit IPAddr(const char *);
    /** Build from textual representation (e.g. 192.168.4.4) */
    explicit IPAddr(const std::string& s)
        : IPAddr(s.c_str()) {}
    /** Build from binary address in network byte order */
    explicit IPAddr(const struct sockaddr *sa);

    IPAddr(const IPAddr&);
    IPAddr& operator=(const IPAddr&);
    ~IPAddr();

    /** Check constructor success */
    bool ok() const;
    /** Returns the address family */
    Family family() const;
    /** Returns the scope type of IPV6 address */
    Scope scopetype() const;
    /** Copies out for use with a system interface */
    /* Zeroes out up to sizeof(sockaddr_storage) */
    bool copyToStorage(struct sockaddr_storage *dest) const;
    /* Copies exactly the needed size */
    bool copyToAddr(struct sockaddr *dest) const;
    
    const struct sockaddr_storage& getaddr() const;
    
    /** Convert to textual representation */
    std::string straddr() const;
    
    friend class Interface;
    class Internal;
private:
    Internal *m;
};

/** An Interface represents a system network interface, its attributes and its
 * addresses. It is usually built by the module internal code by
 * querying the system interfaces */
class Interface {
public:
    enum class Flags {NONE = 0, HASIPV4 = 1, HASIPV6 = 2, LOOPBACK=4,
                      UP=8, MULTICAST=16, HASHWADDR=32};
    Interface();
    Interface(const char *nm);
    Interface(const std::string &nm);
    ~Interface();
    Interface(const Interface&);
    Interface& operator=(const Interface&);

    const std::string& getname() const;
    const std::string& getfriendlyname() const;
    
    /** Return the hardware (ethernet) address as a binary string (can have 
     *  embedded null characters). Empty if no hardware address was
     *  found for this interface */
    const std::string& gethwaddr() const;
    /** Return hardware address in traditional colon-separated hex */
    std::string gethexhwaddr() const;
    bool hasflag(Flags f) const;
    /** Remove all addresses not in the input vector */
    bool trimto(const std::vector<IPAddr>& keep);
    /** Return the first ipv4 address if any, or nullptr */
    const IPAddr *firstipv4addr() const;
    /** Return the first ipv6 address if any, or nullptr */
    const IPAddr *firstipv6addr(
        IPAddr::Scope scope = IPAddr::Scope::Invalid) const;
    /** Return the interface addresses and the corresponding netmasks,
       as parallel arrays */
    const std::pair<const std::vector<IPAddr>&, const std::vector<IPAddr>&>
    getaddresses() const;
    int getindex() const;
    
    /** Print out, a bit like "ip addr" output */
    std::ostream& print(std::ostream&) const;

    class Internal;
    friend class Interfaces;
private:
    Internal *m{nullptr};
};

/** Represent the system's network interfaces. */

class Interfaces {
public:
    /** Return the Interfaces singleton after possibly building it by
     *  querying the system */
    static Interfaces *theInterfaces();

    /** Read state from system again */
    bool refresh();
    
    /** Find interface by name or friendlyname */
    Interface *findByName(const char*nm) const;
    Interface *findByName(const std::string& nm) const{
        return findByName(nm.c_str());
    }

    /** Argument to the select() method: flags which we want or don't */
    struct Filter {
        std::vector<Interface::Flags> needs;
        std::vector<Interface::Flags> rejects;
    };

    /** Return Interface objects satisfying the criteria in f. */
    std::vector<Interface> select(const Filter& f) const;
    
    /** Print out, a bit like "ip addr" output */
    std::ostream& print(std::ostream&);

    /** Find interface address belongs too in input interface vector 
     *  Returns both the interface and the address inside the interface.
     */
    static const Interface *interfaceForAddress(
        const IPAddr& addr, const std::vector<Interface>& vifs,IPAddr& hostaddr);
    /** Find interface address belongs too among all interfaces
     *  Returns both the interface and the address inside the interface. */
    const Interface *interfaceForAddress(const IPAddr& addr, IPAddr& hostaddr);
    
private:
    Interfaces(const Interfaces &) = delete;
    Interfaces& operator=(const Interfaces &) = delete;
    Interfaces();
    ~Interfaces();

    class Internal;
    Internal *m{nullptr};
};

} /* namespace NetIF */

#endif /* _NETIF_H_INCLUDED_ */
