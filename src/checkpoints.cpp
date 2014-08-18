// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/assign/list_of.hpp> // for 'map_list_of()'
#include <boost/foreach.hpp>

#include "checkpoints.h"

#include "main.h"
#include "uint256.h"

namespace Checkpoints
{
    typedef std::map<int, uint256> MapCheckpoints;

    // How many times we expect transactions after the last checkpoint to
    // be slower. This number is a compromise, as it can't be accurate for
    // every system. When reindexing from a fast disk with a slow CPU, it
    // can be up to 20, while when downloading from a slow network with a
    // fast multicore CPU, it won't be much higher than 1.
    static const double fSigcheckVerificationFactor = 5.0;

    struct CCheckpointData {
        const MapCheckpoints *mapCheckpoints;
        int64 nTimeLastCheckpoint;
        int64 nTransactionsLastCheckpoint;
        double fTransactionsPerDay;
    };

    // What makes a good checkpoint block?
    // + Is surrounded by blocks with reasonable timestamps
    //   (no blocks before with a timestamp after, none after with
    //    timestamp before)
    // + Contains no strange transactions
    static MapCheckpoints mapCheckpoints =
        boost::assign::map_list_of
        (    0, uint256("0xc84c8d0f52a7418b28a24e7b5354d6febed47c8cc33b3fa20fdbe4b3a1fcd9c4"))
        (  666, uint256("0x3347d1782965ae18ab2cbfeff089ece5b7f9167ef6a843b959e8ee1e5c0849c1"))
        ( 1333, uint256("0xae8f479d8251f1099272e7ce17688fb84d2f0842655efb53ace678a79b6f5f1a"))
        ( 1669, uint256("0xa5bc32e784d38e95cc23984d2225b6fd14a830065029a6bb53e1366486cfb553"))
        ( 1759, uint256("0x67dbfe2a58176a5e7c640b8ce047776d828c4c3a52dc88653227e56c29cb9409"))
        ( 2021, uint256("0xcb0a506b70a38ef7f301f17a4b14133c50bf9391d8bfe2d6108c177d9436e09e"))
        ( 2375, uint256("0x1819af6faaa15ca12cf51ab67e75507f8d5c41fc8215d458afe6ad62bff90276"))
	;
    static const CCheckpointData data = {
        &mapCheckpoints,
        1408372643, // * UNIX timestamp of last checkpoint block
        4532,       // * total number of transactions between genesis and last checkpoint
                  //   (the tx=... number in the SetBestChain debug.log lines)
        600.0     // * estimated number of transactions per day after checkpoint
    };

    static MapCheckpoints mapCheckpointsTestnet = 
        boost::assign::map_list_of
        ( 0, uint256("0x0"))
        ;
    static const CCheckpointData dataTestnet = {
        &mapCheckpointsTestnet,
        1392757043,
        1,
        300.0
    };

    static MapCheckpoints mapCheckpointsCakenet = 
        boost::assign::map_list_of
        ( 0, uint256("0x0"))
        ;
    static const CCheckpointData dataCakenet = {
        &mapCheckpointsCakenet,
        1392757043,
        1,
        300.0
    };

    const CCheckpointData &Checkpoints() {
        if (fTestNet )
            return dataTestnet;
        else if (fCakeNet )
            return dataCakenet;
        else
            return data;
    }

    bool CheckBlock(int nHeight, const uint256& hash)
    {
        if (fTestNet||fCakeNet) return true; // Testnet has no checkpoints
        if (!GetBoolArg("-checkpoints", false))
            return true;

        const MapCheckpoints& checkpoints = *Checkpoints().mapCheckpoints;

        MapCheckpoints::const_iterator i = checkpoints.find(nHeight);
        if (i == checkpoints.end()) return true;
        return hash == i->second;
    }

    // Guess how far we are in the verification process at the given block index
    double GuessVerificationProgress(CBlockIndex *pindex) {
        if (pindex==NULL)
            return 0.0;

        int64 nNow = time(NULL);

        double fWorkBefore = 0.0; // Amount of work done before pindex
        double fWorkAfter = 0.0;  // Amount of work left after pindex (estimated)
        // Work is defined as: 1.0 per transaction before the last checkoint, and
        // fSigcheckVerificationFactor per transaction after.

        const CCheckpointData &data = Checkpoints();

        if (pindex->nChainTx <= data.nTransactionsLastCheckpoint) {
            double nCheapBefore = pindex->nChainTx;
            double nCheapAfter = data.nTransactionsLastCheckpoint - pindex->nChainTx;
            double nExpensiveAfter = (nNow - data.nTimeLastCheckpoint)/86400.0*data.fTransactionsPerDay;
            fWorkBefore = nCheapBefore;
            fWorkAfter = nCheapAfter + nExpensiveAfter*fSigcheckVerificationFactor;
        } else {
            double nCheapBefore = data.nTransactionsLastCheckpoint;
            double nExpensiveBefore = pindex->nChainTx - data.nTransactionsLastCheckpoint;
            double nExpensiveAfter = (nNow - pindex->nTime)/86400.0*data.fTransactionsPerDay;
            fWorkBefore = nCheapBefore + nExpensiveBefore*fSigcheckVerificationFactor;
            fWorkAfter = nExpensiveAfter*fSigcheckVerificationFactor;
        }

        return fWorkBefore / (fWorkBefore + fWorkAfter);
    }

    int GetTotalBlocksEstimate()
    {
        if (fTestNet||fCakeNet) return 0; // Testnet has no checkpoints
        if (!GetBoolArg("-checkpoints", false))
            return 0;

        const MapCheckpoints& checkpoints = *Checkpoints().mapCheckpoints;

        return checkpoints.rbegin()->first;
    }

    CBlockIndex* GetLastCheckpoint(const std::map<uint256, CBlockIndex*>& mapBlockIndex)
    {
        if (fTestNet||fCakeNet) return NULL; // Testnet has no checkpoints
        if (!GetBoolArg("-checkpoints", false))
            return NULL;

        const MapCheckpoints& checkpoints = *Checkpoints().mapCheckpoints;

        BOOST_REVERSE_FOREACH(const MapCheckpoints::value_type& i, checkpoints)
        {
            const uint256& hash = i.second;
            std::map<uint256, CBlockIndex*>::const_iterator t = mapBlockIndex.find(hash);
            if (t != mapBlockIndex.end())
                return t->second;
        }
        return NULL;
    }
}
