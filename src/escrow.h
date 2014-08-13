#ifndef CERT_H
#define CERT_H

#include "bitcoinrpc.h"
#include "leveldb.h"

class CTransaction;
class CTxOut;
class CValidationState;
class CCoinsViewCache;
class COutPoint;
class CCoins;
class CScript;
class CWalletTx;
class CDiskTxPos;

bool CheckEscrowInputs(CBlockIndex *pindex, const CTransaction &tx, CValidationState &state, CCoinsViewCache &inputs,
                     std::map<std::vector<unsigned char>,uint256> &mapTestPool, bool fBlock, bool fMiner, bool fJustCheck);
bool IsEscrowMine(const CTransaction& tx);
bool IsEscrowMine(const CTransaction& tx, const CTxOut& txout, bool ignore_aliasnew = false);
std::string SendEscrowMoneyWithInputTx(CScript scriptPubKey, int64 nValue, int64 nNetFee, CWalletTx& wtxIn,
                                     CWalletTx& wtxNew, bool fAskFee, const std::string& txData = "");
bool CreateEscrowTransactionWithInputTx(const std::vector<std::pair<CScript, int64> >& vecSend, CWalletTx& wtxIn,
                                      int nTxOut, CWalletTx& wtxNew, CReserveKey& reservekey, int64& nFeeRet, const std::string& txData);
bool DecodeEscrowTx(const CTransaction& tx, int& op, int& nOut, std::vector<std::vector<unsigned char> >& vvch, int nHeight);
bool DecodeEscrowTx(const CCoins& tx, int& op, int& nOut, std::vector<std::vector<unsigned char> >& vvch, int nHeight);
bool DecodeEscrowScript(const CScript& script, int& op, std::vector<std::vector<unsigned char> > &vvch);
bool IsEscrowOp(int op);
int IndexOfEscrowOutput(const CTransaction& tx);
uint64 GetEscrowFeeSubsidy(unsigned int nHeight);
bool GetValueOfEscrowTxHash(const uint256 &txHash, std::vector<unsigned char>& vchValue, uint256& hash, int& nHeight);
int GetEscrowTxHashHeight(const uint256 txHash);
int GetEscrowTxPosHeight(const CDiskTxPos& txPos);
int GetEscrowTxPosHeight2(const CDiskTxPos& txPos, int nHeight);
int GetEscrowDisplayExpirationDepth(int nHeight);
int64 GetEscrowNetworkFee(int seed, int nHeight);
int64 GetEscrowNetFee(const CTransaction& tx);
bool InsertEscrowFee(CBlockIndex *pindex, uint256 hash, uint64 nValue);
bool ExtractEscrowAddress(const CScript& script, std::string& address);

std::string certissuerFromOp(int op);

extern std::map<std::vector<unsigned char>, uint256> mapMyEscrows;
extern std::map<std::vector<unsigned char>, uint256> mapMyEscrowItems;
extern std::map<std::vector<unsigned char>, std::set<uint256> > mapEscrowPending;
extern std::map<std::vector<unsigned char>, std::set<uint256> > mapEscrowItemPending;

class CBitcoinAddress;

class CEscrowItem {
public:
    std::vector<unsigned char> vchRand;
    std::vector<unsigned char> vchTitle;
    std::vector<unsigned char> vchData;
    uint256 txHash;
    uint64 nHeight;
    uint64 nTime;
    uint64 nFee;
    uint256 txPayId;

    CEscrowItem() {
        SetNull();
    }
    IMPLEMENT_SERIALIZE (
        READWRITE(vchRand);
        READWRITE(vchTitle);
        READWRITE(vchData);
        READWRITE(txHash);
        READWRITE(txPayId);
        READWRITE(nHeight);
        READWRITE(nTime);
        READWRITE(nFee);
    )

    friend bool operator==(const CEscrowItem &a, const CEscrowItem &b) {
        return (
        a.vchRand == b.vchRand
        && a.vchTitle == b.vchTitle
        && a.vchData == b.vchData
        && a.txHash == b.txHash
        && a.nHeight == b.nHeight
        && a.nTime == b.nTime
        && a.nFee == b.nFee
        && a.txPayId == b.txPayId
        );
    }

    CEscrowItem operator=(const CEscrowItem &b) {
        vchRand = b.vchRand;
        vchTitle = b.vchTitle;
        vchData = b.vchData;
        txHash = b.txHash;
        nHeight = b.nHeight;
        nTime = b.nTime;
        txPayId = b.txPayId;
        nFee = b.nFee;
        return *this;
    }

    friend bool operator!=(const CEscrowItem &a, const CEscrowItem &b) {
        return !(a == b);
    }

    void SetNull() { nHeight = nTime = 0; txHash = 0; nFee = 0; vchRand.clear(); }
    bool IsNull() const { return (nTime == 0 && txHash == 0 && nFee == 0 && nHeight == 0 && vchRand.size() == 0); }

};

class CEscrow {
public:
    std::vector<unsigned char> vchRand;
    std::vector<unsigned char> vchTitle;
    std::vector<unsigned char> vchData;
    uint256 txHash;
    uint64 nHeight;
    uint64 nTime;
    uint256 hash;
    uint64 n;
    uint64 nFee;
    std::vector<CEscrowItem>certs;

    CEscrow() {
        SetNull();
    }

    IMPLEMENT_SERIALIZE (
        READWRITE(vchRand);
        READWRITE(vchTitle);
        READWRITE(vchData);
        READWRITE(txHash);
        READWRITE(nHeight);
        READWRITE(nTime);
        READWRITE(hash);
        READWRITE(n);
        READWRITE(nFee);
        READWRITE(certs);
    )

    bool GetEscrowItemByHash(std::vector<unsigned char> ahash, CEscrowItem &ca) {
        for(unsigned int i=0;i<certs.size();i++) {
            if(certs[i].vchRand == ahash) {
                ca = certs[i];
                return true;
            }
        }
        return false;
    }

    void PutEscrowItem(CEscrowItem &theOA) {
        for(unsigned int i=0;i<certs.size();i++) {
            CEscrowItem oa = certs[i];
            if(theOA.vchRand == oa.vchRand) {
                certs[i] = theOA;
                return;
            }
        }
        certs.push_back(theOA);
    }

    void PutToEscrowList(std::vector<CEscrow> &escrowList) {
        for(unsigned int i=0;i<escrowList.size();i++) {
            CEscrow o = escrowList[i];
            if(o.nHeight == nHeight) {
                escrowList[i] = *this;
                return;
            }
        }
        escrowList.push_back(*this);
    }

    bool GetEscrowFromList(const std::vector<CEscrow> &escrowList) {
        if(escrowList.size() == 0) return false;
        for(unsigned int i=0;i<escrowList.size();i++) {
            CEscrow o = escrowList[i];
            if(o.nHeight == nHeight) {
                *this = escrowList[i];
                return true;
            }
        }
        *this = escrowList.back();
        return false;
    }

    friend bool operator==(const CEscrow &a, const CEscrow &b) {
        return (
           a.vchRand == b.vchRand
        && a.vchTitle==b.vchTitle
        && a.vchData==b.vchData
        && a.nFee == b.nFee
        && a.n == b.n
        && a.hash == b.hash
        && a.txHash == b.txHash
        && a.nHeight == b.nHeight
        && a.nTime == b.nTime
        && a.certs == b.certs
        );
    }

    CEscrow operator=(const CEscrow &b) {
        vchRand = b.vchRand;
        vchTitle = b.vchTitle;
        vchData = b.vchData;
        nFee = b.nFee;
        n = b.n;
        hash = b.hash;
        txHash = b.txHash;
        nHeight = b.nHeight;
        nTime = b.nTime;
        certs = b.certs;
        return *this;
    }

    friend bool operator!=(const CEscrow &a, const CEscrow &b) {
        return !(a == b);
    }

    void SetNull() { nHeight = n = 0; txHash = hash = 0; certs.clear(); vchRand.clear(); vchTitle.clear(); vchData.clear(); }
    bool IsNull() const { return (n == 0 && txHash == 0 && hash == 0 && nHeight == 0 && vchRand.size() == 0); }

    bool UnserializeFromTx(const CTransaction &tx);
    void SerializeToTx(CTransaction &tx);
    std::string SerializeToString();
};

class CEscrowFee {
public:
    uint256 hash;
    uint64 nHeight;
    uint64 nTime;
    uint64 nFee;

    CEscrowFee() {
        nTime = 0; nHeight = 0; hash = 0; nFee = 0;
    }

    IMPLEMENT_SERIALIZE (
        READWRITE(hash);
        READWRITE(nHeight);
        READWRITE(nTime);
        READWRITE(nFee);
    )

    friend bool operator==(const CEscrowFee &a, const CEscrowFee &b) {
        return (
        a.nTime==b.nTime
        && a.hash==b.hash
        && a.nHeight==b.nHeight
        && a.nFee == b.nFee
        );
    }

    CEscrowFee operator=(const CEscrowFee &b) {
        nTime = b.nTime;
        nFee = b.nFee;
        hash = b.hash;
        nHeight = b.nHeight;
        return *this;
    }

    friend bool operator!=(const CEscrowFee &a, const CEscrowFee &b) { return !(a == b); }
    void SetNull() { hash = nTime = nHeight = nFee = 0;}
    bool IsNull() const { return (nTime == 0 && nFee == 0 && hash == 0 && nHeight == 0); }
};

class CEscrowDB : public CLevelDB {
public:
    CEscrowDB(size_t nCacheSize, bool fMemory, bool fWipe) : CLevelDB(GetDataDir() / "certificates", nCacheSize, fMemory, fWipe) {}

    bool WriteEscrow(const std::vector<unsigned char>& name, std::vector<CEscrow>& vtxPos) {
        return Write(make_pair(std::string("certissueri"), name), vtxPos);
    }

    bool EraseEscrow(const std::vector<unsigned char>& name) {
        return Erase(make_pair(std::string("certissueri"), name));
    }

    bool ReadEscrow(const std::vector<unsigned char>& name, std::vector<CEscrow>& vtxPos) {
        return Read(make_pair(std::string("certissueri"), name), vtxPos);
    }

    bool ExistsEscrow(const std::vector<unsigned char>& name) {
        return Exists(make_pair(std::string("certissueri"), name));
    }

    bool WriteEscrowItem(const std::vector<unsigned char>& name, std::vector<unsigned char>& vchValue) {
        return Write(make_pair(std::string("certissuera"), name), vchValue);
    }

    bool EraseEscrowItem(const std::vector<unsigned char>& name) {
        return Erase(make_pair(std::string("certissuera"), name));
    }

    bool ReadEscrowItem(const std::vector<unsigned char>& name, std::vector<unsigned char>& vchValue) {
        return Read(make_pair(std::string("certissuera"), name), vchValue);
    }

    bool ExistsEscrowItem(const std::vector<unsigned char>& name) {
        return Exists(make_pair(std::string("certissuera"), name));
    }

    bool WriteEscrowFees(std::vector<CEscrowFee>& vtxPos) {
        return Write(make_pair(std::string("certissuera"), std::string("certissuertxf")), vtxPos);
    }

    bool ReadEscrowFees(std::vector<CEscrowFee>& vtxPos) {
        return Read(make_pair(std::string("certissuera"), std::string("certissuertxf")), vtxPos);
    }

    bool ScanEscrows(
            const std::vector<unsigned char>& vchName,
            unsigned int nMax,
            std::vector<std::pair<std::vector<unsigned char>, CEscrow> >& escrowScan);

    bool ReconstructEscrowIndex(CBlockIndex *pindexRescan);
};
extern std::list<CEscrowFee> lstEscrowFees;


bool GetTxOfEscrow(CEscrowDB& dbEscrow, const std::vector<unsigned char> &vchEscrow, CTransaction& tx);

#endif // CERT_H
