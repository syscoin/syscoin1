// Copyright (c) 2010-2011 Vincent Durham
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.
//

#include "init.h"
#include "alias.h"
#include "offer.h"
#include "cert.h"
#include "txdb.h"
#include "util.h"
#include "auxpow.h"
#include "script.h"
#include "ccontroller.h"

using namespace std;

class CCoinControllerBase : public CCoinController
{
public:
    virtual bool IsStandard(const CScript& scriptPubKey) { return false; }
    virtual void AddToWallet(CWalletTx& tx) {}
    virtual bool CheckTransaction(const CTransaction& tx) { return true; }
    virtual bool CheckInputs(CBlockIndex *pindex, const CTransaction &tx, CValidationState &state, CCoinsViewCache &inputs, 
        map<vector<unsigned char>,uint256> &mapTestPool, bool fBlock, bool fMiner, bool fJustCheck) { return true; }
    virtual bool DisconnectInputs(CBlockTreeDB& txdb, const CTransaction& tx, CBlockIndex* pindexBlock) { return true; }
    virtual bool DisconnectBlock(CBlock& block, CBlockTreeDB& txdb, CBlockIndex* pindex) { return true; }
    virtual bool ExtractAddress(const CScript& script, string& address) { return false; }
    void AcceptToMemoryPool(CBlockTreeDB& txdb, const CTransaction& tx) { }
    virtual bool IsMine(const CTransaction& tx) { return false; }
    virtual bool IsMine(const CTransaction& tx, const CTxOut& txout, bool ignore_aliasnew = false) { return false; }
};

