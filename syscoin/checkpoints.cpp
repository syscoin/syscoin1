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
        (     0, uint256("0xc84c8d0f52a7418b28a24e7b5354d6febed47c8cc33b3fa20fdbe4b3a1fcd9c4"))
        (   666, uint256("0x3347d1782965ae18ab2cbfeff089ece5b7f9167ef6a843b959e8ee1e5c0849c1"))
        (  1333, uint256("0xae8f479d8251f1099272e7ce17688fb84d2f0842655efb53ace678a79b6f5f1a"))
        (  1669, uint256("0xa5bc32e784d38e95cc23984d2225b6fd14a830065029a6bb53e1366486cfb553"))
        (  1759, uint256("0x67dbfe2a58176a5e7c640b8ce047776d828c4c3a52dc88653227e56c29cb9409"))
        (  2021, uint256("0xcb0a506b70a38ef7f301f17a4b14133c50bf9391d8bfe2d6108c177d9436e09e"))
        (  2375, uint256("0x1819af6faaa15ca12cf51ab67e75507f8d5c41fc8215d458afe6ad62bff90276"))
        (  2947, uint256("0x586ca99b24ca99b1f3b8d48cb9d29c176065d92d13d84f8420577c7e60256e10"))
        (  3643, uint256("0xd39eb7844ea66caa2f5a5c78cca57623abe6ec4b0102697acabf614a329c417d"))
        (  4719, uint256("0x4cb63627475f1512cb5de607c9efbab40445c61a64fe13998e95700e334ecb16"))
        (  6205, uint256("0x4895b445deb4ecd28d6d62f46e282dc03187a64efcd66f7106cb995df2638fbe"))
        (  8494, uint256("0xd669bab6f606b6e2ef0b4711b18336c2f79ca2b926e867f72040300f09975722"))
        ( 10360, uint256("0xdd8d9937d8bb5784f161407442b84055280c7b843942d35d912db4a7562875f1"))
        ( 12815, uint256("0x5434de9b6916bf5f93f112777bd72b0a3040e705febfd5d00a492468aea29315"))
        ( 14333, uint256("0x1a6052a87543cbdc0f676d00bf7c4ebca757f67c05b6fdc76d41a4d256363bcb"))
        ( 16749, uint256("0x8a3b162afac24e3f68dd7e795f9160b8c3a99d41600ace69d2ce797baada1c2a"))
        ( 19410, uint256("0x5c78ff28da3299a36515eab91e237f3a3fd307cf4400037b97f845db4226c8d9"))
        ( 21367, uint256("0x22ae8cf4fde776105eeda79b8661a2b6e8cc8aa85d7acdbf2a1e88bde167ce2f"))
        ( 23962, uint256("0xa178657581363ca7ea21dc661db65ae29462d98920405008bba5b0e145645e76"))
        ( 26231, uint256("0x782f9a9361d1d6dab203b436253b0d8c7623c8bbafe56f11d8f66af9ff5cafee"))
        ( 29001, uint256("0x6d6a4593fc9f8cf087987f94c5097f079e15c4ae698a09f0f1f7708556901b55"))
        ( 30000, uint256("0xa3a31bc5bf64cedda5faaf77f08b687fac83a2debde33e15f1a1be1d4208711d"))
        ( 31739, uint256("0x296a8c87ab470eaf2906923a70f1f48dae80381cfccb5923a6c6bb1d15895d31"))
        ( 32690, uint256("0xd1f45af1539f37442bda5aa147e441fe6ffa987c679f05fe0423e96e18d83a50"))
        ( 33843, uint256("0x2df0bc28c1647e49d575c5da81f3f991a6501f5ad9134b8f29c0b48dc7acca96"))
        ( 35185, uint256("0x11e952d57dbce33a9573bdcdcad20c083b7250155c05c3cceffa04fed04b6c0d"))
        ( 37882, uint256("0xda3166819d979279fb3967575d72e5b30914eb93e62c02d05b0d4a04c36f52f1"))
        ( 39001, uint256("0x99c471f5487e5a0ce60cdc6d53a9ab58df52e5b9cbd121547265235ddb987058"))
        ( 42315, uint256("0xe693c717c8946c80aea631a30c00315f8811c1fd019b8c792c35eb576fa23f1f"))
        ( 44000, uint256("0x98def7268d9ee11641b73f91593611d9e077e257ebf91a85dbb7a6c15bd8c271"))
        ( 46156, uint256("0x0ceceb0f4cd94f9d7f6e1b90ff1a10fc9fe9ce307428339d8b0910ede31eeaaf"))
        ( 48912, uint256("0xa5504453a92ca1aa324a39dde92756195688bae94adac3e6c1887450e6625c09"))
        ( 50104, uint256("0x06abb38124327da7a156e598fc52927d97be5104a80a62f639d383a485831986"))
        ( 51030, uint256("0x0b6476e61a4ead747ac975b753c4812aaf766ab7effea80d448611ccb717d6ad"))
	;
    static const CCheckpointData data = {
        &mapCheckpoints,
        1411348348, // * UNIX timestamp of last checkpoint block
        85138,      // * total number of transactions between genesis and last checkpoint
                    //   (the tx=... number in the SetBestChain debug.log lines)
        600.0       // * estimated number of transactions per day after checkpoint
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
