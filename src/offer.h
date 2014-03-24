#ifndef OFFER_H
#define OFFER_H

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

bool CheckOfferInputs(CBlockIndex *pindex, const CTransaction &tx, CValidationState &state, CCoinsViewCache &inputs, std::map<uint256,uint256> &mapTestPool, bool fBlock, bool fMiner, bool fJustCheck);
bool ExtractOfferAddress(const CScript& script, std::string& address);
bool IsOfferMine(const CTransaction& tx);
bool IsOfferMine(const CTransaction& tx, const CTxOut& txout, bool ignore_aliasnew = false);
std::string SendOfferMoneyWithInputTx(CScript scriptPubKey, int64 nValue, int64 nNetFee, CWalletTx& wtxIn, CWalletTx& wtxNew, bool fAskFee, const std::string& txData = "");
bool CreateOfferTransactionWithInputTx(const std::vector<std::pair<CScript, int64> >& vecSend, CWalletTx& wtxIn, int nTxOut, CWalletTx& wtxNew, CReserveKey& reservekey, int64& nFeeRet, const std::string& txData);

bool DecodeOfferTx(const CTransaction& tx, int& op, int& nOut, std::vector<std::vector<unsigned char> >& vvch, int nHeight);
bool DecodeOfferTx(const CCoins& tx, int& op, int& nOut, std::vector<std::vector<unsigned char> >& vvch, int nHeight);
bool DecodeOfferScript(const CScript& script, int& op, std::vector<std::vector<unsigned char> > &vvch);
bool IsOfferOp(int op);
int IndexOfOfferOutput(const CTransaction& tx);
uint64 GetOfferTxAvgSubsidy(unsigned int nHeight);
bool GetValueOfOfferTxHash(const uint256 &txHash, std::vector<unsigned char>& vchValue, uint256& hash, int& nHeight);
int GetOfferTxHashHeight(const uint256 txHash);
int GetOfferTxPosHeight(const CDiskTxPos& txPos);
int GetOfferTxPosHeight2(const CDiskTxPos& txPos, int nHeight);
int GetOfferDisplayExpirationDepth(int nHeight);

std::string offerFromOp(int op);

extern std::map<std::vector<unsigned char>, uint256> mapMyOffers;
extern std::map<std::vector<unsigned char>, std::set<uint256> > mapOfferPending;
extern std::map<std::vector<unsigned char>, std::set<uint256> > mapOfferAcceptPending;

class CBitcoinAddress;

class COfferAccept {
public:
	std::vector<unsigned char> vchRand;
    std::vector<unsigned char> vchMessage;
    std::vector<unsigned char> vchAddress;
	uint256 txHash;
	unsigned int nHeight;
	unsigned long nTime;
	int nQty;
	uint64 nPrice;
	bool bPaid;
    uint256 txPayId;

	COfferAccept() {
        SetNull();
    }
    IMPLEMENT_SERIALIZE (
        READWRITE(vchRand);
		READWRITE(txHash);
		READWRITE(nHeight);
    	READWRITE(nTime);
    	READWRITE(nPrice);
    	READWRITE(nQty);
    	READWRITE(bPaid);
        READWRITE(txPayId);
        READWRITE(vchMessage);
        READWRITE(vchAddress);
    )

    friend bool operator==(const COfferAccept &a, const COfferAccept &b) {
        return (
        a.vchRand == b.vchRand
        && a.nPrice == b.nPrice
        && a.nQty == b.nQty
        && a.nTime == b.nTime
        && a.txHash == b.txHash
        && a.nHeight == b.nHeight
        && a.bPaid == b.bPaid
        && a.txPayId == b.txPayId
        );
    }

    COfferAccept operator=(const COfferAccept &b) {
        vchRand = b.vchRand;
        nPrice = b.nPrice;
        nQty = b.nQty ;
        nTime = b.nTime;
        txHash = b.txHash;
        nHeight = b.nHeight;
        bPaid = b.bPaid;
        txPayId = b.txPayId;
        return *this;
    }

    friend bool operator!=(const COfferAccept &a, const COfferAccept &b) {
        return !(a == b);
    }

    void SetNull() { nHeight = nTime = nPrice = nQty = 0; txHash = 0; bPaid = false; }
    bool IsNull() const { return (nTime == 0 && txHash == 0 && nHeight == 0 && nPrice == 0 && nQty == 0 && bPaid == 0); }

};

class COffer {
public:
	std::vector<unsigned char> vchRand;
    std::vector<unsigned char> vchPaymentAddress;
    uint256 txHash;
    unsigned int nHeight;
    uint256 hash;
    unsigned int n;
	std::vector<unsigned char> sCategory;
	std::vector<unsigned char> sTitle;
	std::vector<unsigned char> sDescription;
	uint64 nPrice;
	int nQty;
	std::vector<COfferAccept>accepts;

	COffer() { 
        SetNull();
    }

    IMPLEMENT_SERIALIZE (
        READWRITE(vchRand);
		READWRITE(txHash);
		READWRITE(nHeight);
    	READWRITE(hash);
    	READWRITE(n);
        READWRITE(sCategory);
        READWRITE(sTitle);
        READWRITE(sDescription);
    	READWRITE(nPrice);
    	READWRITE(nQty);
    	READWRITE(accepts);
        READWRITE(vchPaymentAddress);
    )

    bool GetAcceptByHash(std::vector<unsigned char> ahash, COfferAccept &ca) {
    	for(unsigned int i=0;i<accepts.size();i++) {
    		if(accepts[i].vchRand == ahash) {
    			ca = accepts[i];
    			return true;
    		}
    	}
    	return false;
    }

    void PutOfferAccept(COfferAccept &theOA) {
    	for(unsigned int i=0;i<accepts.size();i++) {
    		COfferAccept oa = accepts[i];
    		if(theOA.vchRand == oa.vchRand) {
    			accepts[i] = theOA;
    			return;
    		}
    	}
    	accepts.push_back(theOA);
    }

    void PutToOfferList(std::vector<COffer> &offerList) {
        for(unsigned int i=0;i<offerList.size();i++) {
            COffer o = offerList[i];
            if(o.nHeight == nHeight) {
                offerList[i] = *this;
                return;
            }
        }
        offerList.push_back(*this);
    }

    void GetOfferFromList(std::vector<COffer> &offerList) {
        if(offerList.size() == 0) return;
        for(unsigned int i=0;i<offerList.size();i++) {
            COffer o = offerList[i];
            if(o.nHeight == nHeight) {
                *this = offerList[i];
                return;
            }
        }
        *this = offerList.back();
    }

    int GetRemQty() {
        int nRet = nQty;
        for(unsigned int i=0;i<accepts.size();i++) 
            nRet -= accepts[i].nQty;
        return nRet;
    }

    friend bool operator==(const COffer &a, const COffer &b) {
        return (
           a.vchRand == b.vchRand
        && a.sCategory==b.sCategory
        && a.sTitle == b.sTitle 
        && a.sDescription == b.sDescription 
        && a.nPrice == b.nPrice 
        && a.nQty == b.nQty 
        && a.n == b.n
        && a.hash == b.hash
        && a.txHash == b.txHash
        && a.nHeight == b.nHeight
        && a.accepts == b.accepts
        && a.vchPaymentAddress == b.vchPaymentAddress
        );
    }

    COffer operator=(const COffer &b) {
    	vchRand = b.vchRand;
        sCategory = b.sCategory;
        sTitle = b.sTitle;
        sDescription = b.sDescription;
        nPrice = b.nPrice;
        nQty = b.nQty;
        n = b.n;
        hash = b.hash;
        txHash = b.txHash;
        nHeight = b.nHeight;
        accepts = b.accepts;
        vchPaymentAddress = b.vchPaymentAddress;
        return *this;
    }

    friend bool operator!=(const COffer &a, const COffer &b) {
        return !(a == b);
    }
    
    void SetNull() { nHeight = n = nPrice = nQty = 0; txHash = hash = 0; accepts.clear(); vchRand.clear(); sTitle.clear(); sDescription.clear();}
    bool IsNull() const { return (n == 0 && txHash == 0 && hash == 0 && nHeight == 0 && nPrice == 0 && nQty == 0); }

    bool UnserializeFromTx(const CTransaction &tx);
    void SerializeToTx(CTransaction &tx);
    std::string SerializeToString();
};


//class COfferIndex {
//public:
//	uint256 hash;
//    unsigned int n;
//    uint256 txHash;
//    int nHeight;
//    std::vector<unsigned char> vValue;
//
//    COfferIndex() {
//        SetNull();
//    }
//
//    COfferIndex(uint256 txHashIn, uint256 o, unsigned int i, unsigned int nHeightIn, std::vector<unsigned char> vValueIn) {
//        txHash = txHashIn;
//        hash = o;
//        n = i;
//        nHeight = nHeightIn;
//        vValue = vValueIn;
//    }
//
//    IMPLEMENT_SERIALIZE (
//        READWRITE(txHash);
//        READWRITE(hash);
//    	READWRITE(n);
//        READWRITE(VARINT(nHeight));
//    	READWRITE(vValue);
//    )
//
//    friend bool operator==(const COfferIndex &a, const COfferIndex &b) {
//        return (a.nHeight == b.nHeight && a.txHash == b.txHash);
//    }
//
//    friend bool operator!=(const COfferIndex &a, const COfferIndex &b) {
//        return !(a == b);
//    }
//
//    void SetNull() { txHash.SetHex("0x0"); nHeight = -1; vValue.clear(); }
//    bool IsNull() const { return (nHeight == -1 && txHash == 0); }
//};

class COfferTxnValue {
public:
	unsigned int nBlockTime;
	unsigned int nHeight;
	uint256 hash;
	uint64 nValue;

	COfferTxnValue() {
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

	COfferTxnValue(uint256 s, unsigned int t, unsigned int h, uint64 v) {
		hash = s;
		nBlockTime = t;
		nHeight = h;
		nValue = v;
	}
	bool operator()(uint256 s, unsigned int t, unsigned int h, uint64 v) {
		hash = s;
		nBlockTime = t;
		nHeight = h;
		nValue = v;
		return true;
	}
    friend bool operator==(const COfferTxnValue &a, const COfferTxnValue &b) {
        return (a.hash == b.hash && a.nBlockTime == b.nBlockTime && a.nHeight == b.nHeight && a.nValue == b.nValue);
    }

    friend bool operator!=(const COfferTxnValue &a, const COfferTxnValue &b) {
        return !(a == b);
    }
};
extern std::list<COfferTxnValue> lstOfferTxValues;


class COfferDB : public CLevelDB {
public:
	COfferDB(size_t nCacheSize, bool fMemory, bool fWipe) : CLevelDB(GetDataDir() / "offers", nCacheSize, fMemory, fWipe) {
    }

	bool WriteOffer(const std::vector<unsigned char>& name, std::vector<COffer>& vtxPos) {
		return Write(make_pair(std::string("offeri"), name), vtxPos);
	}

	bool EraseOffer(const std::vector<unsigned char>& name) {
	    return Erase(make_pair(std::string("offeri"), name));
	}

	bool ReadOffer(const std::vector<unsigned char>& name, std::vector<COffer>& vtxPos) {
		return Read(make_pair(std::string("offeri"), name), vtxPos);
	}

	bool ExistsOffer(const std::vector<unsigned char>& name) {
	    return Exists(make_pair(std::string("offeri"), name));
	}

	bool WriteOfferAccept(const std::vector<unsigned char>& name, std::vector<unsigned char>& vchValue) {
		return Write(make_pair(std::string("offera"), name), vchValue);
	}

	bool EraseOfferAccept(const std::vector<unsigned char>& name) {
	    return Erase(make_pair(std::string("offera"), name));
	}

	bool ReadOfferAccept(const std::vector<unsigned char>& name, std::vector<unsigned char>& vchValue) {
		return Read(make_pair(std::string("offera"), name), vchValue);
	}

	bool ExistsOfferAccept(const std::vector<unsigned char>& name) {
	    return Exists(make_pair(std::string("offera"), name));
	}

	bool WriteOfferTxFees(std::vector<COfferTxnValue>& vtxPos) {
		return Write(std::string("offertxf"), vtxPos);
	}
	bool ReadOfferTxFees(std::vector<COfferTxnValue>& vtxPos) {
		return Read(std::string("offertxf"), vtxPos);
	}

    bool ScanOffers(
            const std::vector<unsigned char>& vchName,
            int nMax,
            std::vector<std::pair<std::vector<unsigned char>, COffer> >& offerScan);

    bool ReconstructOfferIndex();
};


bool GetTxOfOffer(COfferDB& dbOffer, const std::vector<unsigned char> &vchOffer, CTransaction& tx);

#endif // OFFER_H
