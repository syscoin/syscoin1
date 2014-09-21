#ifndef NAMEDB_H
#define NAMEDB_H

#include "bitcoinrpc.h"
#include "leveldb.h"

class CAssetIndex {
public:
    uint256 txHash;
    int64 nHeight;
    COutPoint txPrevOut;
    std::vector<unsigned char> vValue;

    CAssetIndex() { 
        SetNull();
    }

    CAssetIndex(uint256 txHashIn, COutPoint txPrevOutIn, uint64 nHeightIn, std::vector<unsigned char> vValueIn) {
        txHash = txHashIn;
        txPrevOut = txPrevOutIn;
        nHeight = nHeightIn;
        vValue = vValueIn;
    }

    IMPLEMENT_SERIALIZE (
        READWRITE(txHash);
        READWRITE(VARINT(nHeight));
        READWRITE(txPrevOut);
    	READWRITE(vValue);
    )

    friend bool operator==(const CAssetIndex &a, const CAssetIndex &b) {
        return (a.nHeight == b.nHeight && a.txHash == b.txHash);
    }

    friend bool operator!=(const CAssetIndex &a, const CAssetIndex &b) {
        return !(a == b);
    }
    
    void SetNull() { txHash.SetHex("0x0"); nHeight = -1; vValue.clear(); }
    bool IsNull() const { return (nHeight == -1 && txHash == 0); }
};

class CAssetFee {
public:
	uint64 nBlockTime;
	uint64 nHeight;
	uint256 hash;
	uint64 nValue;

	CAssetFee() {
        SetNull();
    }

    IMPLEMENT_SERIALIZE (
        READWRITE(nBlockTime);
        READWRITE(nHeight);
        READWRITE(hash);
    	READWRITE(nValue);
    )

    void SetNull() { hash = 0; nHeight = 0; nValue = 0; nBlockTime = 0; }
    bool IsNull() const { return (nHeight == 0 && hash == 0 && nValue == 0 && nBlockTime == 0); }

	CAssetFee(uint256 s, uint64 t, uint64 h, uint64 v) {
		hash = s;
		nBlockTime = t;
		nHeight = h;
		nValue = v;
	}
	bool operator()(uint256 s, uint64 t, uint64 h, uint64 v) {
		hash = s;
		nBlockTime = t;
		nHeight = h;
		nValue = v;
		return true;
	}
    friend bool operator==(const CAssetFee &a, const CAssetFee &b) {
        return (a.hash == b.hash && a.nBlockTime == b.nBlockTime && a.nHeight == b.nHeight && a.nValue == b.nValue);
    }

    friend bool operator!=(const CAssetFee &a, const CAssetFee &b) {
        return !(a == b);
    }
};
extern std::list<CAssetFee> lstAssetFees;

class CAssetDB : public CLevelDB {
public:
    CAssetDB(size_t nCacheSize, bool fMemory, bool fWipe) : CLevelDB(GetDataDir() / "assets", nCacheSize, fMemory, fWipe) {
    }

	bool WriteAsset(const std::vector<unsigned char>& name, std::vector<CAssetIndex>& vtxPos) {
		return Write(make_pair(std::string("asseti"), name), vtxPos);
	}

	bool EraseAsset(const std::vector<unsigned char>& name) {
	    return Erase(make_pair(std::string("asseti"), name));
	}
	bool ReadAsset(const std::vector<unsigned char>& name, std::vector<CAssetIndex>& vtxPos) {
		return Read(make_pair(std::string("asseti"), name), vtxPos);
	}
	bool ExistsAsset(const std::vector<unsigned char>& name) {
	    return Exists(make_pair(std::string("asseti"), name));
	}

	bool WriteAssetTxFees(std::vector<CAssetFee>& vtxPos) {
		return Write(std::string("assettxf"), vtxPos);
	}
	bool ReadAssetTxFees(std::vector<CAssetFee>& vtxPos) {
		return Read(std::string("assettxf"), vtxPos);
	}

    bool WriteAssetIndex(std::vector<std::vector<unsigned char> >& vtxIndex) {
        return Write(std::string("assetndx"), vtxIndex);
    }
    bool ReadAssetIndex(std::vector<std::vector<unsigned char> >& vtxIndex) {
        return Read(std::string("assetndx"), vtxIndex);
    }

    bool ScanAssets(
            const std::vector<unsigned char>& vchAsset,
            unsigned int nMax,
            std::vector<std::pair<std::vector<unsigned char>, CAssetIndex> >& nameScan);

    bool ReconstructAssetIndex(CBlockIndex *pindexRescan);
};



extern std::map<std::vector<unsigned char>, uint256> mapMyAssets;
extern std::map<std::vector<unsigned char>, std::set<uint256> > mapAssetsPending;

std::string stringFromVch(const std::vector<unsigned char> &vch);
std::vector<unsigned char> vchFromValue(const json_spirit::Value& value);
std::vector<unsigned char> vchFromString(const std::string &str);
std::string stringFromValue(const json_spirit::Value& value);

static const int SYSCOIN_TX_VERSION = 0x7400;
static const int64 MIN_AMOUNT = COIN;
static const unsigned int MAX_NAME_LENGTH = 255;
static const unsigned int MAX_VALUE_LENGTH = 1023;
static const unsigned int MIN_ACTIVATE_DEPTH = 120;

bool CheckAssetInputs(
    CBlockIndex *pindex, const CTransaction &tx, CValidationState &state,
	CCoinsViewCache &inputs, std::map<std::vector<unsigned char>,uint256> &mapTestPool, 
    bool fBlock, bool fMiner, bool fJustCheck);
bool ExtractAssetAddress(const CScript& script, std::string& address);
bool IsAssetMine(const CTransaction& tx);
void RemoveAssetTxnFromMemoryPool(const CTransaction& tx);
bool IsAssetMine(const CTransaction& tx, const CTxOut& txout, bool ignore_assetnew = false);
bool IsAssetOp(int op);

int GetAssetTxPosHeight(const CAssetIndex& txPos);
int GetAssetTxPosHeight(const CDiskTxPos& txPos);
int GetAssetTxPosHeight2(const CDiskTxPos& txPos, int nHeight);
bool GetTxOfAsset(CAssetDB& dbAsset, const std::vector<unsigned char> &vchAsset, CTransaction& tx);
int IndexOfAssetOutput(const CTransaction& tx);
bool GetValueOfAssetTxHash(const uint256& txHash, std::vector<unsigned char>& vchValue, uint256& hash, int& nHeight);
bool GetAssetOfTx(const CTransaction& tx, std::vector<unsigned char>& name);
bool GetValueOfAssetTx(const CTransaction& tx, std::vector<unsigned char>& value);
bool DecodeAssetTx(const CTransaction& tx, int& op, int& nOut, std::vector<std::vector<unsigned char> >& vvch, int nHeight);
bool GetValueOfAssetTx(const CCoins& tx, std::vector<unsigned char>& value);
bool DecodeAssetTx(const CCoins& tx, int& op, int& nOut, std::vector<std::vector<unsigned char> >& vvch, int nHeight);
bool DecodeAssetScript(const CScript& script, int& op, std::vector<std::vector<unsigned char> > &vvch);
bool GetAssetAddress(const CTransaction& tx, std::string& strAddress);
bool GetAssetAddress(const CDiskTxPos& txPos, std::string& strAddress);
std::string SendAssetMoneyWithInputTx(CScript scriptPubKey, int64 nValue, int64 nNetFee, CWalletTx& wtxIn, CWalletTx& wtxNew, bool fAskFee, const std::string& txData = "");
bool CreateAssetTransactionWithInputTx(const std::vector<std::pair<CScript, int64> >& vecSend, CWalletTx& wtxIn, int nTxOut, CWalletTx& wtxNew, CReserveKey& reservekey, int64& nFeeRet, const std::string& txData = "");
int64 GetAssetNetworkFee(int nType, int nHeight);
uint64 GetAssetFeeSubsidy(const unsigned int nTime);
int64 GetAssetNetFee(const CTransaction& tx);
bool InsertAssetFee(CBlockIndex *pindex, uint256 hash, uint64 nValue);
std::string assetFromOp(int op);
bool IsAssetOp(int op);
int GetAssetDisplayExpirationDepth(int nHeight);
void UnspendInputs(CWalletTx& wtx);

#endif // NAMEDB_H
