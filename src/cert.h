#ifndef CERT_H
#define CERT_H

#include "bitcoinrpc.h"
#include "leveldb.h"
#include "script.h"
class CTransaction;
class CTxOut;
class CValidationState;
class CCoinsViewCache;
class COutPoint;
class CCoins;
class CScript;
class CWalletTx;
class CDiskTxPos;

bool CheckCertInputs(CBlockIndex *pindex, const CTransaction &tx, CValidationState &state, CCoinsViewCache &inputs, bool fBlock, bool fMiner, bool fJustCheck);
bool IsCertMine(const CTransaction& tx);
bool IsCertMine(const CTransaction& tx, const CTxOut& txout);
std::string SendCertMoneyWithInputTx(std::vector<std::pair<CScript, int64> >& vecSend, int64 nValue, int64 nNetFee, CWalletTx& wtxIn, CWalletTx& wtxNew, 
    bool fAskFee, const std::string& txData = "");
std::string SendCertMoneyWithInputTx(CScript scriptPubKey, int64 nValue, int64 nNetFee, CWalletTx& wtxIn,
                                     CWalletTx& wtxNew, bool fAskFee, const std::string& txData = "");
bool CreateCertTransactionWithInputTx(const std::vector<std::pair<CScript, int64> >& vecSend, CWalletTx& wtxIn,
                                      int nTxOut, CWalletTx& wtxNew, CReserveKey& reservekey, int64& nFeeRet, const std::string& txData);
bool DecodeCertTx(const CTransaction& tx, int& op, int& nOut, std::vector<std::vector<unsigned char> >& vvch, int nHeight);
bool DecodeCertTx(const CCoins& tx, int& op, int& nOut, std::vector<std::vector<unsigned char> >& vvch, int nHeight);
bool DecodeCertScript(const CScript& script, int& op, std::vector<std::vector<unsigned char> > &vvch);
bool IsCertOp(int op);
int IndexOfCertOutput(const CTransaction& tx);
uint64 GetCertFeeSubsidy(unsigned int nHeight);
bool GetValueOfCertTxHash(const uint256 &txHash, std::vector<unsigned char>& vchValue, uint256& hash, int& nHeight);
int64 GetCertTxHashHeight(const uint256 txHash);
int GetCertTxPosHeight(const CDiskTxPos& txPos);
int GetCertTxPosHeight2(const CDiskTxPos& txPos, int nHeight);
int GetCertDisplayExpirationDepth(int nHeight);
int64 GetCertNetworkFee(opcodetype seed);
int64 GetCertNetFee(const CTransaction& tx);
bool InsertCertFee(CBlockIndex *pindex, uint256 hash, uint64 nValue);
bool ExtractCertAddress(const CScript& script, std::string& address);

std::string certFromOp(int op);


class CBitcoinAddress;

class CCert {
public:
    std::vector<unsigned char> vchRand;
    std::vector<unsigned char> vchTitle;
    std::vector<unsigned char> vchData;
    uint256 txHash;
    uint64 nHeight;
    uint64 nTime;
    uint64 nFee;

    CCert() {
        SetNull();
    }
    CCert(const CTransaction &tx) {
        SetNull();
        UnserializeFromTx(tx);
    }
    IMPLEMENT_SERIALIZE (
        READWRITE(vchRand);
        READWRITE(vchTitle);
        READWRITE(vchData);
        READWRITE(txHash);
        READWRITE(nHeight);
        READWRITE(nTime);
        READWRITE(nFee);
    )

    friend bool operator==(const CCert &a, const CCert &b) {
        return (
        a.vchRand == b.vchRand
        && a.vchTitle == b.vchTitle
        && a.vchData == b.vchData
        && a.txHash == b.txHash
        && a.nHeight == b.nHeight
        && a.nTime == b.nTime
        && a.nFee == b.nFee
        );
    }

    CCert operator=(const CCert &b) {
        vchRand = b.vchRand;
        vchTitle = b.vchTitle;
        vchData = b.vchData;
        txHash = b.txHash;
        nHeight = b.nHeight;
        nTime = b.nTime;
        nFee = b.nFee;
        return *this;
    }

    friend bool operator!=(const CCert &a, const CCert &b) {
        return !(a == b);
    }

    void SetNull() { nHeight = nTime = 0; txHash = 0; nFee = 0; vchRand.clear(); }
    bool IsNull() const { return (nTime == 0 && txHash == 0 && nFee == 0 && nHeight == 0 && vchRand.size() == 0); }
    bool UnserializeFromTx(const CTransaction &tx);
    void SerializeToTx(CTransaction &tx);
    std::string SerializeToString();
};


class CCertFee {
public:
    uint256 hash;
    uint64 nHeight;
    uint64 nTime;
    uint64 nFee;

    CCertFee() {
        nTime = 0; nHeight = 0; hash = 0; nFee = 0;
    }

    IMPLEMENT_SERIALIZE (
        READWRITE(hash);
        READWRITE(nHeight);
        READWRITE(nTime);
        READWRITE(nFee);
    )

    friend bool operator==(const CCertFee &a, const CCertFee &b) {
        return (
        a.nTime==b.nTime
        && a.hash==b.hash
        && a.nHeight==b.nHeight
        && a.nFee == b.nFee
        );
    }

    CCertFee operator=(const CCertFee &b) {
        nTime = b.nTime;
        nFee = b.nFee;
        hash = b.hash;
        nHeight = b.nHeight;
        return *this;
    }

    friend bool operator!=(const CCertFee &a, const CCertFee &b) { return !(a == b); }
    void SetNull() { hash = nTime = nHeight = nFee = 0;}
    bool IsNull() const { return (nTime == 0 && nFee == 0 && hash == 0 && nHeight == 0); }
};
bool RemoveCertFee(CCertFee &txnVal);

class CCertDB : public CLevelDB {
public:
    CCertDB(size_t nCacheSize, bool fMemory, bool fWipe) : CLevelDB(GetDataDir() / "certificates", nCacheSize, fMemory, fWipe) {}

    bool WriteCert(const std::vector<unsigned char>& name, std::vector<CCert>& vtxPos) {
        return Write(make_pair(std::string("certi"), name), vtxPos);
    }

    bool EraseCert(const std::vector<unsigned char>& name) {
        return Erase(make_pair(std::string("certi"), name));
    }

    bool ReadCert(const std::vector<unsigned char>& name, std::vector<CCert>& vtxPos) {
        return Read(make_pair(std::string("certi"), name), vtxPos);
    }

    bool ExistsCert(const std::vector<unsigned char>& name) {
        return Exists(make_pair(std::string("certi"), name));
    }

    bool WriteCertFees(std::vector<CCertFee>& vtxPos) {
        return Write(make_pair(std::string("certa"), std::string("certtxf")), vtxPos);
    }

    bool ReadCertFees(std::vector<CCertFee>& vtxPos) {
        return Read(make_pair(std::string("certa"), std::string("certtxf")), vtxPos);
    }

    bool ScanCerts(
            const std::vector<unsigned char>& vchName,
            unsigned int nMax,
            std::vector<std::pair<std::vector<unsigned char>, CCert> >& certScan);

    bool ReconstructCertIndex(CBlockIndex *pindexRescan);
};
extern std::list<CCertFee> lstCertFees;


bool GetTxOfCert(CCertDB& dbCert, const std::vector<unsigned char> &vchCert, CTransaction& tx);

#endif // CERT_H
