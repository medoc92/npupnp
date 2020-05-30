#ifndef CLIENT_TABLE_H
#define CLIENT_TABLE_H

#ifdef INCLUDE_CLIENT_APIS

#include <string>
#include <list>

struct ClientSubscription {
    int renewEventId{-1};
    std::string SID;
    std::string actualSID;
    std::string eventURL;
    ClientSubscription& operator=(const ClientSubscription& other) {
        if (this != &other) {
            SID = other.SID;
            actualSID = other.actualSID;
            eventURL = other.eventURL;
            this->renewEventId = -1;
        }
        return *this;
    }
};

void RemoveClientSubClientSID(std::list<ClientSubscription>& lst,
                              const std::string& sid);
ClientSubscription *GetClientSubClientSID(std::list<ClientSubscription>& lst,
                                          const std::string& sid);
ClientSubscription *GetClientSubActualSID(std::list<ClientSubscription>& lst,
                                          const std::string& sid);
#endif /* INCLUDE_CLIENT_APIS */


#endif /* CLIENT_TABLE_H */

