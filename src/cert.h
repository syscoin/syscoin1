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

bool CheckCertTxnInputs(CBlockIndex *pindex, const CTransaction &tx, CValidationState &state, CCoinsViewCache &inputs, std::map<uint256,uint256> &mapTestPool, bool fBlock, bool fMiner, bool fJustCheck);
bool ExtractCertAddress(const CScript& script, std::string& address);
bool IsCertTxnMine(const CTransaction& tx);
bool IsCertTxnMine(const CTransaction& tx, const CTxOut& txout, bool ignore_aliasnew = false);
std::string SendCertMoneyWithInputTx(CScript scriptPubKey, int64 nValue, int64 nNetFee, CWalletTx& wtxIn, CWalletTx& wtxNew, bool fAskFee, const std::string& txData = "");
bool CreateCertTransactionWithInputTx(const std::vector<std::pair<CScript, int64> >& vecSend, CWalletTx& wtxIn, int nTxOut, CWalletTx& wtxNew, CReserveKey& reservekey, int64& nFeeRet, const std::string& txData);

bool DecodeCertTx(const CTransaction& tx, int& op, int& nOut, std::vector<std::vector<unsigned char> >& vvch, int nHeight);
bool DecodeCertTx(const CCoins& tx, int& op, int& nOut, std::vector<std::vector<unsigned char> >& vvch, int nHeight);
bool DecodeCertScript(const CScript& script, int& op, std::vector<std::vector<unsigned char> > &vvch);
bool IsCertOp(int op);
int IndexOfCertTxnOutput(const CTransaction& tx);
uint64 GetCertTxnFeeSubsidy(unsigned int nHeight);
bool GetValueOfCertTxHash(const uint256 &txHash, std::vector<unsigned char>& vchValue, uint256& hash, int& nHeight);
int IndexOfCertHashHeight(const uint256 txHash);
int GetCertTxPosHeight(const CDiskTxPos& txPos);
int GetCertTxPosHeight2(const CDiskTxPos& txPos, int nHeight);
int GetCertIssuerDisplayExpirationDepth(int nHeight);
int64 GetCertNetworkFee(int seed, int nHeight);
int64 GetCertNetFee(const CTransaction& tx);
bool InsertCertFee(CBlockIndex *pindex, uint256 hash, uint64 nValue);

std::string certFromOp(int op);

extern std::map<std::vector<unsigned char>, uint256> mapMyCerts;
extern std::map<std::vector<unsigned char>, uint256> mapMyCerts;
extern std::map<std::vector<unsigned char>, std::set<uint256> > mapCertPending;
extern std::map<std::vector<unsigned char>, std::set<uint256> > mapCertPending;
extern std::vector<std::vector<unsigned char> > vecCertIndex;

class CBitcoinAddress;

class CCert {
public:
	std::vector<unsigned char> vchRand;
	std::vector<unsigned char> vchTitle;
	std::vector<unsigned char> vchData;
	uint256 txHash;
	unsigned int nHeight;
	unsigned long nTime;
	uint64 nFee;

	CCert() {
        SetNull();
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

    void SetNull() { nHeight = nTime = 0; txHash = 0; vchRand.clear(); vchTitle.clear(); vchData.clear(); }
    bool IsNull() const { return (nTime == 0 && txHash == 0 && nHeight == 0 && nFee == 0 && !vchRand.size() && !vchTitle.size() && !vchData.size() ; }

};

class CCertIssuer {
public:
	std::vector<unsigned char> vchRand;
    std::vector<unsigned char> vchTitle;
    std::vector<unsigned char> vchDescription;
    std::vector<unsigned char> vchData;
    uint256 txHash;
    unsigned int nHeight;
    unsigned long nTime;
    uint256 hash;
    unsigned int n;
	int64 nFee;
	std::vector<CCert>certs;

	CCertIssuer() { 
        SetNull();
    }

    IMPLEMENT_SERIALIZE (
        READWRITE(vchRand);
        READWRITE(vchTitle);
        READWRITE(vchDescription);
        READWRITE(vchData);
		READWRITE(txHash);
		READWRITE(nHeight);
		READWRITE(nTime);
    	READWRITE(hash);
    	READWRITE(n);
    	READWRITE(nFee);
    	READWRITE(certs);
    )

    bool GetAcceptByHash(std::vector<unsigned char> ahash, CCert &ca) {
    	for(unsigned int i=0;i<accepts.size();i++) {
    		if(accepts[i].vchRand == ahash) {
    			ca = accepts[i];
    			return true;
    		}
    	}
    	return false;
    }

    void PutCert(CCert &theOA) {
    	for(unsigned int i=0;i<accepts.size();i++) {
    		CCert oa = accepts[i];
    		if(theOA.vchRand == oa.vchRand) {
    			accepts[i] = theOA;
    			return;
    		}
    	}
    	accepts.push_back(theOA);
    }

    void PutToIssuerList(std::vector<CCertIssuer> &certList) {
        for(unsigned int i=0;i<certList.size();i++) {
            CCertIssuer o = certList[i];
            if(o.nHeight == nHeight) {
                certList[i] = *this;
                return;
            }
        }
        certList.push_back(*this);
    }

    bool GetCertIssuerFromList(const std::vector<CCertIssuer> &certList) {
        if(certList.size() == 0) return false;
        for(unsigned int i=0;i<certList.size();i++) {
            CCertIssuer o = certList[i];
            if(o.nHeight == nHeight) {
                *this = certList[i];
                return true;
            }
        }
        *this = certList.back();
        return false;
    }

    int GetRemQty() {
        int nRet = nQty;
        for(unsigned int i=0;i<accepts.size();i++) 
            nRet -= accepts[i].nQty;
        return nRet;
    }

    friend bool operator==(const CCertIssuer &a, const CCertIssuer &b) {
        return (
           a.vchRand == b.vchRand
        && a.vchData==b.vchData
        && a.vchTitle == b.vchTitle 
        && a.vchDescription == b.vchDescription 
        && a.nFee == b.nFee
        && a.n == b.n
        && a.hash == b.hash
        && a.txHash == b.txHash
        && a.nHeight == b.nHeight
        && a.nTime == b.nTime
        && a.accepts == b.certs
        );
    }

    CCertIssuer operator=(const CCertIssuer &b) {
    	vchRand = b.vchRand;
        vchData = b.vchData;
        vchTitle = b.vchTitle;
        vchDescription = b.vchDescription;
        nFee = b.nFee;
        n = b.n;
        hash = b.hash;
        txHash = b.txHash;
        nHeight = b.nHeight;
        nTime = b.nTime;
        certs = b.certs;
        return *this;
    }

    friend bool operator!=(const CCertIssuer &a, const CCertIssuer &b) {
        return !(a == b);
    }
    
    void SetNull() { nHeight = n = 0; txHash = hash = 0; certs.clear(); vchRand.clear(); 
    	vchData.clear(); vchDescription.clear(); vchTitle.clear(); }
    bool IsNull() const { return (n == 0 && txHash == 0 && hash == 0 
    	&& nHeight == 0 && !vchRand.size() && !vchTitle.size() && !vchData.size() && !vchDescription.size()); }

    bool UnserializeFromTx(const CTransaction &tx);
    void SerializeToTx(CTransaction &tx);
    std::string SerializeToString();
};

class CCertFee {
public:
	uint256 hash;
	unsigned int nHeight;
	unsigned long nTime;
	int64 nFee;

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

class CCertDB : public CLevelDB {
public:
	CCertDB(size_t nCacheSize, bool fMemory, bool fWipe) : CLevelDB(GetDataDir() / "certs", nCacheSize, fMemory, fWipe) {}

	bool WriteCertIssuer(const std::vector<unsigned char>& name, std::vector<CCertIssuer>& vtxPos) {
		return Write(make_pair(std::string("certi"), name), vtxPos);
	}

	bool EraseCertIssuer(const std::vector<unsigned char>& name) {
	    return Erase(make_pair(std::string("certi"), name));
	}

	bool ReadCertIssuer(const std::vector<unsigned char>& name, std::vector<CCertIssuer>& vtxPos) {
		return Read(make_pair(std::string("certi"), name), vtxPos);
	}

	bool ExistsCertIssuer(const std::vector<unsigned char>& name) {
	    return Exists(make_pair(std::string("certi"), name));
	}

	bool WriteCert(const std::vector<unsigned char>& name, std::vector<unsigned char>& vchValue) {
		return Write(make_pair(std::string("certl"), name), vchValue);
	}

	bool EraseCert(const std::vector<unsigned char>& name) {
	    return Erase(make_pair(std::string("certl"), name));
	}

	bool ReadCert(const std::vector<unsigned char>& name, std::vector<unsigned char>& vchValue) {
		return Read(make_pair(std::string("certl"), name), vchValue);
	}

	bool ExistsCert(const std::vector<unsigned char>& name) {
	    return Exists(make_pair(std::string("certl"), name));
	}

	bool WriteCertTxFees(std::vector<CCertFee>& vtxPos) {
		return Write(make_pair(std::string("certa"), std::string("certtxf")), vtxPos);
	}

	bool ReadCertTxFees(std::vector<CCertFee>& vtxPos) {
		return Read(make_pair(std::string("certa"), std::string("certtxf")), vtxPos);
	}

    bool WriteCertIssuerIndex(std::vector<std::vector<unsigned char> >& vtxPos) {
        return Write(make_pair(std::string("certa"), std::string("certndx")), vtxPos);
    }

    bool ReadCertIssuerIndex(std::vector<std::vector<unsigned char> >& vtxPos) {
        return Read(make_pair(std::string("certa"), std::string("certndx")), vtxPos);
    }

    bool ScanCerts(
            const std::vector<unsigned char>& vchName,
            int nMax,
            std::vector<std::pair<std::vector<unsigned char>, CCertIssuer> >& certScan);

    bool ReconstructCertIndex(CBlockIndex *pindexRescan);
};
extern std::list<CCertFee> lstCertFees;


bool GetTxOfCert(CCertDB& dbCert, const std::vector<unsigned char> &vchCert, CTransaction& tx);

#endif // CERT_H
