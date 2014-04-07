// Copyright (c) 2010-2011 Vincent Durham
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CCONTROLLER_H
#define BITCOIN_CCONTROLLER_H

class CAliasIndex;

class CCoinController
{
public:
    virtual bool IsStandard(const CScript& scriptPubKey) = 0;
    virtual void AddToWallet(CWalletTx& tx) = 0;
    virtual bool CheckTransaction(const CTransaction& tx) = 0;
    virtual bool CheckInputs(CBlockIndex *pindex, const CTransaction &tx, CValidationState &state, CCoinsViewCache &inputs, 
        std::map<std::vector<unsigned char>,uint256> &mapTestPool, bool fBlock, bool fMiner, bool fJustCheck) = 0;
    virtual bool DisconnectInputs(CBlockTreeDB& txdb, const CTransaction& tx, CBlockIndex* pindexBlock) = 0;
    virtual bool DisconnectBlock(CBlock& block, CBlockTreeDB& txdb, CBlockIndex* pindex) = 0;
    virtual bool ExtractAddress(const CScript& script, std::string& address) = 0;
    virtual void AcceptToMemoryPool(CBlockTreeDB& txdb, const CTransaction& tx) = 0;

    /* These are for display and wallet management purposes.  Not for use to decide
     * whether to spend a coin. */
    virtual bool IsMine(const CTransaction& tx) = 0;
    virtual bool IsMine(const CTransaction& tx, const CTxOut& txout, bool ignore_aliasnew = false) = 0;
};

extern CCoinController* InitCoinController();

#endif
