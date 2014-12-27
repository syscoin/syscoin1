// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef _BITCOINHTTP_H_
#define _BITCOINHTTP_H_ 1

void StartHTTPThreads();
void StopHTTPThreads();

class CHTTPContentType
{
public:
    std::string ext;
    std::string type;
};


#endif
