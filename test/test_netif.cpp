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
#include "netif.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <getopt.h>

#include <string>
#include <iostream>

using namespace std;

static int       op_flags;
#define OPT_i    0x1
static struct option long_options[] = {
    {"interface", required_argument, 0, 'i'},
    {0, 0, 0, 0}
};

static char *thisprog;
static char usage [] =
    " Default: dump interface configuration\n"
    "-i <testaddr>  : find interface for address.\n"
    ;

static void
Usage(void)
{
    fprintf(stderr, "%s: usage:\n%s", thisprog, usage);
    exit(1);
}

int main(int argc, char *argv[])
{
    thisprog = argv[0];
    int ret;
    std::string addr;
    while ((ret = getopt_long(argc, argv, "i:",
                              long_options, NULL)) != -1) {
        switch (ret) {
        case 'i': addr = optarg;op_flags |= OPT_i; break;
        default:
            Usage();
        }
    }
    NetIF::Interfaces *ifs = NetIF::Interfaces::theInterfaces();
    if (op_flags == 0) {
        ifs->print(std::cout);
    } else if (op_flags & OPT_i) {
        NetIF::IPAddr ipaddr(addr);
        if (!ipaddr.ok()) {
            std::cerr << "Address parse failed for [" << addr << "]\n";
            return 1;
        }

        NetIF::Interfaces::Filter filt;
        std::vector<NetIF::Interface> vifs = ifs->select(filt);
        NetIF::IPAddr haddr;
        const NetIF::Interface *the_if =
            NetIF::Interfaces::interfaceForAddress(ipaddr, vifs, haddr);

        if (nullptr == the_if) {
            std::cerr << "No interface found for " << ipaddr.straddr() << "\n";
            return 1;
        }
        std::cout << "Interface for " << ipaddr.straddr() << " : \n\n";
        the_if->print(std::cout);
        std::cout << " \nhost address: " << haddr.straddr() << "\n";
    } else if (0) {
        NetIF::Interfaces::Filter filt{
            .needs={NetIF::Interface::Flags::HASIPV4},
            .rejects={NetIF::Interface::Flags::LOOPBACK}};
        std::vector<NetIF::Interface> myifs = ifs->select(filt);
        cout << "\nSelected:\n";
        for (const auto& entry : myifs)
            entry.print(std::cout);
    } else {
        Usage();
    }
    return 0;
}
