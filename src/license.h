#ifndef LICENSE_H
#define LICENSE_H

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

bool CheckLicenseTxnInputs(CBlockIndex *pindex, const CTransaction &tx, CValidationState &state, CCoinsViewCache &inputs, std::map<uint256,uint256> &mapTestPool, bool fBlock, bool fMiner, bool fJustCheck);
bool ExtractLicenseAddress(const CScript& script, std::string& address);
bool IsLicenseTxnMine(const CTransaction& tx);
bool IsLicenseTxnMine(const CTransaction& tx, const CTxOut& txout, bool ignore_aliasnew = false);
std::string SendLicenseMoneyWithInputTx(CScript scriptPubKey, int64 nValue, int64 nNetFee, CWalletTx& wtxIn, CWalletTx& wtxNew, bool fAskFee, const std::string& txData = "");
bool CreateLicenseTransactionWithInputTx(const std::vector<std::pair<CScript, int64> >& vecSend, CWalletTx& wtxIn, int nTxOut, CWalletTx& wtxNew, CReserveKey& reservekey, int64& nFeeRet, const std::string& txData);

bool DecodeLicenseTx(const CTransaction& tx, int& op, int& nOut, std::vector<std::vector<unsigned char> >& vvch, int nHeight);
bool DecodeLicenseTx(const CCoins& tx, int& op, int& nOut, std::vector<std::vector<unsigned char> >& vvch, int nHeight);
bool DecodeLicenseScript(const CScript& script, int& op, std::vector<std::vector<unsigned char> > &vvch);
bool IsLicenseOp(int op);
int IndexOfLicenseTxnOutput(const CTransaction& tx);
uint64 GetLicenseTxnFeeSubsidy(unsigned int nHeight);
bool GetValueOfLicenseTxHash(const uint256 &txHash, std::vector<unsigned char>& vchValue, uint256& hash, int& nHeight);
int IndexOfLicenseHashHeight(const uint256 txHash);
int GetLicenseTxPosHeight(const CDiskTxPos& txPos);
int GetLicenseTxPosHeight2(const CDiskTxPos& txPos, int nHeight);
int GetLicenseIssuerDisplayExpirationDepth(int nHeight);
int64 GetLicenseNetworkFee(int seed, int nHeight);
int64 GetLicenseNetFee(const CTransaction& tx);
bool InsertLicenseFee(CBlockIndex *pindex, uint256 hash, uint64 nValue);

std::string licenseFromOp(int op);

extern std::map<std::vector<unsigned char>, uint256> mapMyLicenses;
extern std::map<std::vector<unsigned char>, uint256> mapMyLicenses;
extern std::map<std::vector<unsigned char>, std::set<uint256> > mapLicensePending;
extern std::map<std::vector<unsigned char>, std::set<uint256> > mapLicensePending;
extern std::vector<std::vector<unsigned char> > vecLicenseIndex;

class CBitcoinAddress;

class CLicense {
public:
	std::vector<unsigned char> vchRand;
	std::vector<unsigned char> vchTitle;
	std::vector<unsigned char> vchData;
	uint256 txHash;
	unsigned int nHeight;
	unsigned long nTime;
	uint64 nFee;

	CLicense() {
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

    friend bool operator==(const CLicense &a, const CLicense &b) {
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

    CLicense operator=(const CLicense &b) {
        vchRand = b.vchRand;
        vchTitle = b.vchTitle;
        vchData = b.vchData;
        txHash = b.txHash;
        nHeight = b.nHeight;
        nTime = b.nTime;
        nFee = b.nFee;
        return *this;
    }

    friend bool operator!=(const CLicense &a, const CLicense &b) {
        return !(a == b);
    }

    void SetNull() { nHeight = nTime = 0; txHash = 0; vchRand.clear(); vchTitle.clear(); vchData.clear(); }
    bool IsNull() const { return (nTime == 0 && txHash == 0 && nHeight == 0 && nFee == 0 && !vchRand.size() && !vchTitle.size() && !vchData.size() ; }

};

class CLicenseIssuer {
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
	std::vector<CLicense>licenses;

	CLicenseIssuer() { 
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
    	READWRITE(licenses);
    )

    bool GetAcceptByHash(std::vector<unsigned char> ahash, CLicense &ca) {
    	for(unsigned int i=0;i<accepts.size();i++) {
    		if(accepts[i].vchRand == ahash) {
    			ca = accepts[i];
    			return true;
    		}
    	}
    	return false;
    }

    void PutLicense(CLicense &theOA) {
    	for(unsigned int i=0;i<accepts.size();i++) {
    		CLicense oa = accepts[i];
    		if(theOA.vchRand == oa.vchRand) {
    			accepts[i] = theOA;
    			return;
    		}
    	}
    	accepts.push_back(theOA);
    }

    void PutToIssuerList(std::vector<CLicenseIssuer> &licenseList) {
        for(unsigned int i=0;i<licenseList.size();i++) {
            CLicenseIssuer o = licenseList[i];
            if(o.nHeight == nHeight) {
                licenseList[i] = *this;
                return;
            }
        }
        licenseList.push_back(*this);
    }

    bool GetLicenseIssuerFromList(const std::vector<CLicenseIssuer> &licenseList) {
        if(licenseList.size() == 0) return false;
        for(unsigned int i=0;i<licenseList.size();i++) {
            CLicenseIssuer o = licenseList[i];
            if(o.nHeight == nHeight) {
                *this = licenseList[i];
                return true;
            }
        }
        *this = licenseList.back();
        return false;
    }

    int GetRemQty() {
        int nRet = nQty;
        for(unsigned int i=0;i<accepts.size();i++) 
            nRet -= accepts[i].nQty;
        return nRet;
    }

    friend bool operator==(const CLicenseIssuer &a, const CLicenseIssuer &b) {
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
        && a.accepts == b.licenses
        );
    }

    CLicenseIssuer operator=(const CLicenseIssuer &b) {
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
        licenses = b.licenses;
        return *this;
    }

    friend bool operator!=(const CLicenseIssuer &a, const CLicenseIssuer &b) {
        return !(a == b);
    }
    
    void SetNull() { nHeight = n = 0; txHash = hash = 0; licenses.clear(); vchRand.clear(); 
    	vchData.clear(); vchDescription.clear(); vchTitle.clear(); }
    bool IsNull() const { return (n == 0 && txHash == 0 && hash == 0 
    	&& nHeight == 0 && !vchRand.size() && !vchTitle.size() && !vchData.size() && !vchDescription.size()); }

    bool UnserializeFromTx(const CTransaction &tx);
    void SerializeToTx(CTransaction &tx);
    std::string SerializeToString();
};

class CLicenseFee {
public:
	uint256 hash;
	unsigned int nHeight;
	unsigned long nTime;
	int64 nFee;

	CLicenseFee() {
		nTime = 0; nHeight = 0; hash = 0; nFee = 0;
	}

	IMPLEMENT_SERIALIZE (
	    READWRITE(hash);
	    READWRITE(nHeight);
		READWRITE(nTime);
		READWRITE(nFee);
	)

    friend bool operator==(const CLicenseFee &a, const CLicenseFee &b) {
        return (
        a.nTime==b.nTime
        && a.hash==b.hash
        && a.nHeight==b.nHeight
        && a.nFee == b.nFee 
        );
    }

    CLicenseFee operator=(const CLicenseFee &b) {
        nTime = b.nTime;
        nFee = b.nFee;
        hash = b.hash;
        nHeight = b.nHeight;
        return *this;
    }

    friend bool operator!=(const CLicenseFee &a, const CLicenseFee &b) { return !(a == b); }
	void SetNull() { hash = nTime = nHeight = nFee = 0;}
    bool IsNull() const { return (nTime == 0 && nFee == 0 && hash == 0 && nHeight == 0); }
};

class CLicenseDB : public CLevelDB {
public:
	CLicenseDB(size_t nCacheSize, bool fMemory, bool fWipe) : CLevelDB(GetDataDir() / "licenses", nCacheSize, fMemory, fWipe) {}

	bool WriteLicenseIssuer(const std::vector<unsigned char>& name, std::vector<CLicenseIssuer>& vtxPos) {
		return Write(make_pair(std::string("licensei"), name), vtxPos);
	}

	bool EraseLicenseIssuer(const std::vector<unsigned char>& name) {
	    return Erase(make_pair(std::string("licensei"), name));
	}

	bool ReadLicenseIssuer(const std::vector<unsigned char>& name, std::vector<CLicenseIssuer>& vtxPos) {
		return Read(make_pair(std::string("licensei"), name), vtxPos);
	}

	bool ExistsLicenseIssuer(const std::vector<unsigned char>& name) {
	    return Exists(make_pair(std::string("licensei"), name));
	}

	bool WriteLicense(const std::vector<unsigned char>& name, std::vector<unsigned char>& vchValue) {
		return Write(make_pair(std::string("licensel"), name), vchValue);
	}

	bool EraseLicense(const std::vector<unsigned char>& name) {
	    return Erase(make_pair(std::string("licensel"), name));
	}

	bool ReadLicense(const std::vector<unsigned char>& name, std::vector<unsigned char>& vchValue) {
		return Read(make_pair(std::string("licensel"), name), vchValue);
	}

	bool ExistsLicense(const std::vector<unsigned char>& name) {
	    return Exists(make_pair(std::string("licensel"), name));
	}

	bool WriteLicenseTxFees(std::vector<CLicenseFee>& vtxPos) {
		return Write(make_pair(std::string("licensea"), std::string("licensetxf")), vtxPos);
	}

	bool ReadLicenseTxFees(std::vector<CLicenseFee>& vtxPos) {
		return Read(make_pair(std::string("licensea"), std::string("licensetxf")), vtxPos);
	}

    bool WriteLicenseIssuerIndex(std::vector<std::vector<unsigned char> >& vtxPos) {
        return Write(make_pair(std::string("licensea"), std::string("licensendx")), vtxPos);
    }

    bool ReadLicenseIssuerIndex(std::vector<std::vector<unsigned char> >& vtxPos) {
        return Read(make_pair(std::string("licensea"), std::string("licensendx")), vtxPos);
    }

    bool ScanLicenses(
            const std::vector<unsigned char>& vchName,
            int nMax,
            std::vector<std::pair<std::vector<unsigned char>, CLicenseIssuer> >& licenseScan);

    bool ReconstructLicenseIndex(CBlockIndex *pindexRescan);
};
extern std::list<CLicenseFee> lstLicenseFees;


bool GetTxOfLicense(CLicenseDB& dbLicense, const std::vector<unsigned char> &vchLicense, CTransaction& tx);

#endif // LICENSE_H
