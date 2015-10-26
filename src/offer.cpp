#include "offer.h"
#include "init.h"
#include "txdb.h"
#include "util.h"
#include "auxpow.h"
#include "script.h"
#include "main.h"
#include "bitcoinrpc.h"
#include "json/json_spirit_reader_template.h"
#include "json/json_spirit_writer_template.h"
#include <boost/xpressive/xpressive_dynamic.hpp>

using namespace std;
using namespace json_spirit;
extern const CRPCTable tableRPC;
template<typename T> void ConvertTo(Value& value, bool fAllowNull = false);

extern bool ExistsInMempool(std::vector<unsigned char> vchNameOrRand, opcodetype type);
extern bool HasReachedMainNetForkB2();

extern COfferDB *pofferdb;
extern CCertDB *pcertdb;
extern uint256 SignatureHash(CScript scriptCode, const CTransaction& txTo,
		unsigned int nIn, int nHashType);
extern int CheckTransactionAtRelativeDepth(CBlockIndex* pindexBlock,
        const CCoins *txindex, int maxDepth);
extern int GetCertExpirationDepth();
CScript RemoveOfferScriptPrefix(const CScript& scriptIn);
extern CScript RemoveCertScriptPrefix(const CScript& scriptIn);
bool DecodeOfferScript(const CScript& script, int& op,
		std::vector<std::vector<unsigned char> > &vvch,
		CScript::const_iterator& pc);

extern bool Solver(const CKeyStore& keystore, const CScript& scriptPubKey,
		uint256 hash, int nHashType, CScript& scriptSigRet,
		txnouttype& whichTypeRet);
extern bool VerifyScript(const CScript& scriptSig, const CScript& scriptPubKey,
		const CTransaction& txTo, unsigned int nIn, unsigned int flags,
		int nHashType);

extern bool GetTxOfCert(CCertDB& dbCert, const vector<unsigned char> &vchCert,
        CCert &txPos, CTransaction& tx);
extern bool GetCertAddress(const CTransaction& tx, std::string& strAddress);
extern string getCurrencyToSYSFromAlias(const vector<unsigned char> &vchCurrency, int64 &nFee, const unsigned int &nHeightToFind, vector<string>& rateList, int &precision);
int64 convertCurrencyCodeToSyscoin(const vector<unsigned char> &vchCurrencyCode, const double &nPrice, const unsigned int &nHeight, int &precision)
{
	double sysPrice = nPrice;
	int64 nRate;
	vector<string> rateList;
	if(getCurrencyToSYSFromAlias(vchCurrencyCode, nRate, nHeight, rateList, precision) == "")
	{
		// nRate is assumed to be rate of USD/SYS
		sysPrice = sysPrice * nRate;
	}
	return (int64)sysPrice;
}

// check wallet transactions to see if there was a refund for an accept already
// need this because during a reorg blocks are disconnected (deleted from db) and we can't rely on looking in db to see if refund was made for an accept
bool foundRefundInWallet(const vector<unsigned char> &vchAcceptRand, const vector<unsigned char>& acceptCode)
{
    TRY_LOCK(pwalletMain->cs_wallet, cs_trylock);
    BOOST_FOREACH(PAIRTYPE(const uint256, CWalletTx)& item, pwalletMain->mapWallet)
    {
		vector<vector<unsigned char> > vvchArgs;
		int op, nOut;
        const CWalletTx& wtx = item.second;
        if (wtx.IsCoinBase() || !wtx.IsFinal())
            continue;
		if (DecodeOfferTx(wtx, op, nOut, vvchArgs, -1))
		{
			if(op == OP_OFFER_REFUND)
			{
				if(vchAcceptRand == vvchArgs[1] && vvchArgs[2] == acceptCode)
				{
					return true;
				}
			}
		}
	}
	return false;
}
bool foundOfferLinkInWallet(const vector<unsigned char> &vchOffer, const vector<unsigned char> &vchAcceptRandLink)
{
    TRY_LOCK(pwalletMain->cs_wallet, cs_trylock);
    BOOST_FOREACH(PAIRTYPE(const uint256, CWalletTx)& item, pwalletMain->mapWallet)
    {
		vector<vector<unsigned char> > vvchArgs;
		int op, nOut;
        const CWalletTx& wtx = item.second;
        if (wtx.IsCoinBase() || !wtx.IsFinal())
            continue;
		if (DecodeOfferTx(wtx, op, nOut, vvchArgs, -1))
		{
			if(op == OP_OFFER_ACCEPT)
			{
				if(vvchArgs[0] == vchOffer)
				{
					COffer theOffer(wtx);
					COfferAccept theOfferAccept;
					if (theOffer.IsNull())
						continue;

					if(theOffer.GetAcceptByHash(vvchArgs[1], theOfferAccept))
					{
						if(theOfferAccept.vchLinkOfferAccept == vchAcceptRandLink)
							return true;
					}
				}
			}
		}
	}
	return false;
}
unsigned int QtyOfPendingAcceptsInMempool(const vector<unsigned char>& vchToFind)
{
	unsigned int nQty = 0;
	for (map<uint256, CTransaction>::iterator mi = mempool.mapTx.begin();
		mi != mempool.mapTx.end(); ++mi) {
		CTransaction& tx = (*mi).second;
		if (tx.IsCoinBase() || !tx.IsFinal())
			continue;
		vector<vector<unsigned char> > vvch;
		int op, nOut;
		
		if(DecodeOfferTx(tx, op, nOut, vvch, -1)) {
			if(op == OP_OFFER_ACCEPT)
			{
				if(vvch[0] == vchToFind)
				{
					COffer theOffer(tx);
					COfferAccept theOfferAccept;
					if (theOffer.IsNull())
						continue;
					// if offer is already confirmed dont account for it in the mempool
					if (GetTxHashHeight(tx.GetHash()) > 0) 
						continue;
					if(theOffer.GetAcceptByHash(vvch[1], theOfferAccept))
					{
						nQty += theOfferAccept.nQty;
					}
				}
			}
		}		
	}
	return nQty;

}

// refund an offer accept by creating a transaction to send coins to offer accepter, and an offer accept back to the offer owner. 2 Step process in order to use the coins that were sent during initial accept.
string makeOfferLinkAcceptTX(COfferAccept& theOfferAccept, const vector<unsigned char> &vchOffer, const vector<unsigned char> &vchOfferAcceptLink)
{
	string strError;
	string strMethod = string("offeraccept");
	Array params;
	Value result;
	CPubKey newDefaultKey;
	
	if(foundOfferLinkInWallet(vchOffer, vchOfferAcceptLink))
	{
		if(fDebug)
			printf("makeOfferLinkAcceptTX() offer linked transaction already exists\n");
		return "";
	}
	pwalletMain->GetKeyFromPool(newDefaultKey, false);
	CBitcoinAddress refundAddr = CBitcoinAddress(newDefaultKey.GetID());
	const vector<unsigned char> vchRefundAddress = vchFromString(refundAddr.ToString());
	params.push_back(stringFromVch(vchOffer));
	params.push_back(static_cast<ostringstream*>( &(ostringstream() << theOfferAccept.nQty) )->str());
	params.push_back(stringFromVch(theOfferAccept.vchMessage));
	params.push_back(stringFromVch(vchRefundAddress));
	params.push_back(stringFromVch(vchOfferAcceptLink));
    try {
        tableRPC.execute(strMethod, params);
	}
	catch (Object& objError)
	{
		return find_value(objError, "message").get_str().c_str();
	}
	catch(std::exception& e)
	{
		return string(e.what()).c_str();
	}
	return "";

}
// refund an offer accept by creating a transaction to send coins to offer accepter, and an offer accept back to the offer owner. 2 Step process in order to use the coins that were sent during initial accept.
string makeOfferRefundTX(const CTransaction& prevTx, COffer& theOffer, const COfferAccept& theOfferAccept, const vector<unsigned char> &refundCode)
{
	if(!pwalletMain)
	{
		return string("makeOfferRefundTX(): no wallet found");
	}
	if(theOfferAccept.bRefunded)
	{
		return string("This offer accept has already been refunded");
	}	
	CWalletTx wtxPrevIn;

	if (!pwalletMain->GetTransaction(prevTx.GetHash(), wtxPrevIn)) 
	{
		return string("makeOfferRefundTX() : can't find this offer in your wallet");
	}

	const vector<unsigned char> &vchOffer = theOffer.vchRand;
	const vector<unsigned char> &vchAcceptRand = theOfferAccept.vchRand;

	// this is a syscoin txn
	CWalletTx wtx, wtx2;
	int64 nTotalValue = MIN_AMOUNT;
	wtx.nVersion = SYSCOIN_TX_VERSION;
	CScript scriptPubKeyOrig, scriptPayment;

	if(refundCode == OFFER_REFUND_COMPLETE)
	{
		int precision = 2;
		COfferLinkWhitelistEntry entry;
		theOffer.linkWhitelist.GetLinkEntryByHash(theOfferAccept.vchCertLink, entry);
		// lookup the price of the offer in syscoin based on pegged alias at the block # when accept was made (sets nHeight in offeraccept serialized object in tx)
		nTotalValue = convertCurrencyCodeToSyscoin(theOffer.sCurrencyCode, theOffer.GetPrice(entry), theOfferAccept.nHeight, precision)*theOfferAccept.nQty;
		set<pair<const CWalletTx*,unsigned int> > setCoins;
		int64 nValueIn = 0;
		if (!pwalletMain->SelectCoins(nTotalValue, setCoins, nValueIn))
		{
			return string("insufficient funds to pay for offer refund");
		}
	} 


	// create OFFERACCEPT txn keys
	CScript scriptPubKey;
    CPubKey newDefaultKey;
    pwalletMain->GetKeyFromPool(newDefaultKey, false);
	scriptPubKeyOrig.SetDestination(newDefaultKey.GetID());

	scriptPubKey << CScript::EncodeOP_N(OP_OFFER_REFUND)
			<< vchOffer << vchAcceptRand << refundCode
			<< OP_2DROP << OP_2DROP;
	scriptPubKey += scriptPubKeyOrig;
	
	if (ExistsInMempool(vchOffer, OP_OFFER_ACTIVATE) || ExistsInMempool(vchOffer, OP_OFFER_UPDATE)) {
		return string("there are pending operations or refunds on that offer");
	}

	if(foundRefundInWallet(vchAcceptRand, refundCode))
	{
		return string("foundRefundInWallet - This offer accept has already been refunded");
	}
    // add a copy of the offer object with just
    // the one accept object to save bandwidth
    COffer offerCopy = theOffer;
    COfferAccept offerAcceptCopy = theOfferAccept;
    offerCopy.accepts.clear();
    offerCopy.PutOfferAccept(offerAcceptCopy);
	string bdata = offerCopy.SerializeToString();
	string strError = SendOfferMoneyWithInputTx(scriptPubKey, MIN_AMOUNT, 0,
				wtxPrevIn, wtx, false, bdata);
	if (strError != "")
	{
		return string(strError);
	}
	if(refundCode == OFFER_REFUND_COMPLETE)
	{
		vector< pair<CScript, int64> > vecSend;
		CBitcoinAddress refundAddress(stringFromVch(theOfferAccept.vchRefundAddress));
		scriptPayment.SetDestination(refundAddress.Get());
		vecSend.push_back(make_pair(scriptPayment, nTotalValue));		
		string strError = pwalletMain->SendMoney(vecSend, nTotalValue,
				wtx2, false);
		if (strError != "")
		{
			return string(strError);
		}
	}	
	return "";

}

bool IsOfferOp(int op) {
	return op == OP_OFFER_ACTIVATE
        || op == OP_OFFER_UPDATE
        || op == OP_OFFER_ACCEPT
		|| op == OP_OFFER_REFUND;
}



int64 GetOfferNetworkFee(opcodetype seed, unsigned int nHeight) {

	int64 nFee = 0;
	int64 nRate = 0;
	const vector<unsigned char> &vchCurrency = vchFromString("USD");
	vector<string> rateList;
	int precision;
	if(getCurrencyToSYSFromAlias(vchCurrency, nRate, nHeight, rateList,precision) != "")
	{
		if(seed==OP_OFFER_ACTIVATE) {
    		nFee = 50 * COIN;
		}
		else if(seed==OP_OFFER_UPDATE) {
    		nFee = 100 * COIN;
		}
	}
	else
	{
		// 10 pips USD, 10k pips = $1USD
		nFee = nRate/1000;
	}
	// Round up to CENT
	nFee += CENT - 1;
	nFee = (nFee / CENT) * CENT;
	return nFee;
}




int GetOfferExpirationDepth() {
    return 525600;
}

// For display purposes, pass the name height.
int GetOfferDisplayExpirationDepth() {
    return GetOfferExpirationDepth();
}

bool IsMyOffer(const CTransaction& tx, const CTxOut& txout) {
	const CScript& scriptPubKey = RemoveOfferScriptPrefix(txout.scriptPubKey);
	CScript scriptSig;
	txnouttype whichTypeRet;
	if (!Solver(*pwalletMain, scriptPubKey, 0, 0, scriptSig, whichTypeRet))
		return false;
    return true;
}

string offerFromOp(int op) {
	switch (op) {
	case OP_OFFER_ACTIVATE:
		return "offeractivate";
	case OP_OFFER_UPDATE:
		return "offerupdate";
	case OP_OFFER_ACCEPT:
		return "offeraccept";
	case OP_OFFER_REFUND:
		return "offerrefund";
	default:
		return "<unknown offer op>";
	}
}

bool COffer::UnserializeFromTx(const CTransaction &tx) {
	try {
		CDataStream dsOffer(vchFromString(DecodeBase64(stringFromVch(tx.data))), SER_NETWORK, PROTOCOL_VERSION);
		dsOffer >> *this;
	} catch (std::exception &e) {
		return false;
	}
	return true;
}
string COffer::SerializeToString() {
	// serialize offer object
	CDataStream dsOffer(SER_NETWORK, PROTOCOL_VERSION);
	dsOffer << *this;
	vector<unsigned char> vchData(dsOffer.begin(), dsOffer.end());
	return EncodeBase64(vchData.data(), vchData.size());
}

//TODO implement
bool COfferDB::ScanOffers(const std::vector<unsigned char>& vchOffer, unsigned int nMax,
		std::vector<std::pair<std::vector<unsigned char>, COffer> >& offerScan) {
    leveldb::Iterator *pcursor = pofferdb->NewIterator();

    CDataStream ssKeySet(SER_DISK, CLIENT_VERSION);
    ssKeySet << make_pair(string("offeri"), vchOffer);
    string sType;
    pcursor->Seek(ssKeySet.str());

    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        try {
            leveldb::Slice slKey = pcursor->key();
            CDataStream ssKey(slKey.data(), slKey.data() + slKey.size(), SER_DISK, CLIENT_VERSION);

            ssKey >> sType;
            if(sType == "offeri") {
            	vector<unsigned char> vchOffer;
                ssKey >> vchOffer;
                leveldb::Slice slValue = pcursor->value();
                CDataStream ssValue(slValue.data(), slValue.data() + slValue.size(), SER_DISK, CLIENT_VERSION);
                vector<COffer> vtxPos;
                ssValue >> vtxPos;
                COffer txPos;
                if (!vtxPos.empty())
                    txPos = vtxPos.back();
                offerScan.push_back(make_pair(vchOffer, txPos));
            }
            if (offerScan.size() >= nMax)
                break;

            pcursor->Next();
        } catch (std::exception &e) {
            return error("%s() : deserialize error", __PRETTY_FUNCTION__);
        }
    }
    delete pcursor;
    return true;
}

/**
 * [COfferDB::ReconstructOfferIndex description]
 * @param  pindexRescan [description]
 * @return              [description]
 */
bool COfferDB::ReconstructOfferIndex(CBlockIndex *pindexRescan) {
    CBlockIndex* pindex = pindexRescan;  
	if(!HasReachedMainNetForkB2())
		return true;
    {
	TRY_LOCK(pwalletMain->cs_wallet, cs_trylock);
    while (pindex) {  

        int nHeight = pindex->nHeight;
        CBlock block;
        block.ReadFromDisk(pindex);
        uint256 txblkhash;
        
        BOOST_FOREACH(CTransaction& tx, block.vtx) {
            if (tx.nVersion != SYSCOIN_TX_VERSION)
                continue;

            vector<vector<unsigned char> > vvchArgs;
            int op, nOut;

            // decode the offer op, params, height
            bool o = DecodeOfferTx(tx, op, nOut, vvchArgs, -1);
            if (!o || !IsOfferOp(op)) continue;         
            vector<unsigned char> vchOffer = vvchArgs[0];
        
            // get the transaction
            if(!GetTransaction(tx.GetHash(), tx, txblkhash, true))
                continue;

            // attempt to read offer from txn
            COffer txOffer;
            COfferAccept txCA;
            if(!txOffer.UnserializeFromTx(tx))
				return error("ReconstructOfferIndex() : failed to unserialize offer from tx");

			// save serialized offer
			COffer serializedOffer = txOffer;

            // read offer from DB if it exists
            vector<COffer> vtxPos;
            if (ExistsOffer(vchOffer)) {
                if (!ReadOffer(vchOffer, vtxPos))
                    return error("ReconstructOfferIndex() : failed to read offer from DB");
                if(vtxPos.size()!=0) {
                	txOffer.nHeight = nHeight;
                	txOffer.GetOfferFromList(vtxPos);
                }
            }
			// use the txn offer as master on updates,
			// but grab the accepts from the DB first
			if(op == OP_OFFER_UPDATE || op == OP_OFFER_REFUND) {
				serializedOffer.accepts = txOffer.accepts;
				txOffer = serializedOffer;
			}

			if(op == OP_OFFER_REFUND)
			{
            	bool bReadOffer = false;
            	vector<unsigned char> vchOfferAccept = vvchArgs[1];
	            if (ExistsOfferAccept(vchOfferAccept)) {
	                if (!ReadOfferAccept(vchOfferAccept, vchOffer))
	                    printf("ReconstructOfferIndex() : warning - failed to read offer accept from offer DB\n");
	                else bReadOffer = true;
	            }
				if(!bReadOffer && !txOffer.GetAcceptByHash(vchOfferAccept, txCA))
					 return error("ReconstructOfferIndex() OP_OFFER_REFUND: failed to read offer accept from serializedOffer\n");

				if(vvchArgs[2] == OFFER_REFUND_COMPLETE){
					txCA.bRefunded = true;
					txCA.txRefundId = tx.GetHash();
				}	
				txCA.bPaid = true;
                txCA.vchRand = vvchArgs[1];
		        txCA.nTime = pindex->nTime;
		        txCA.txHash = tx.GetHash();
		        txCA.nHeight = nHeight;
				txOffer.PutOfferAccept(txCA);
				
			}
            // read the offer accept from db if exists
            if(op == OP_OFFER_ACCEPT) {
            	bool bReadOffer = false;
            	vector<unsigned char> vchOfferAccept = vvchArgs[1];
	            if (ExistsOfferAccept(vchOfferAccept)) {
	                if (!ReadOfferAccept(vchOfferAccept, vchOffer))
	                    printf("ReconstructOfferIndex() : warning - failed to read offer accept from offer DB\n");
	                else bReadOffer = true;
	            }
				if(!bReadOffer && !txOffer.GetAcceptByHash(vchOfferAccept, txCA))
					 return error("ReconstructOfferIndex() OP_OFFER_ACCEPT: failed to read offer accept from offer\n");

				// add txn-specific values to offer accept object
				txCA.bPaid = true;
                txCA.vchRand = vvchArgs[1];
		        txCA.nTime = pindex->nTime;
		        txCA.txHash = tx.GetHash();
		        txCA.nHeight = nHeight;
				txOffer.PutOfferAccept(txCA);
			}


			if(op == OP_OFFER_ACTIVATE || op == OP_OFFER_UPDATE || op == OP_OFFER_REFUND) {
				txOffer.txHash = tx.GetHash();
	            txOffer.nHeight = nHeight;
			}


			// txn-specific values to offer object
            txOffer.vchRand = vvchArgs[0];
            txOffer.nTime = pindex->nTime;
            txOffer.PutToOfferList(vtxPos);

            if (!WriteOffer(vchOffer, vtxPos))
                return error("ReconstructOfferIndex() : failed to write to offer DB");
            if(op == OP_OFFER_ACCEPT || op == OP_OFFER_REFUND)
	            if (!WriteOfferAccept(vvchArgs[1], vvchArgs[0]))
	                return error("ReconstructOfferIndex() : failed to write to offer DB");
			
			if(fDebug)
				printf( "RECONSTRUCT OFFER: op=%s offer=%s title=%s qty=%u hash=%s height=%d\n",
					offerFromOp(op).c_str(),
					stringFromVch(vvchArgs[0]).c_str(),
					stringFromVch(txOffer.sTitle).c_str(),
					txOffer.nQty,
					tx.GetHash().ToString().c_str(), 
					nHeight);
			
        }
        pindex = pindex->pnext;
        
    }
	Flush();
    }
    return true;
}

// get the depth of transaction txnindex relative to block at index pIndexBlock, looking
// up to maxdepth. Return relative depth if found, or -1 if not found and maxdepth reached.
int CheckTransactionAtRelativeDepth(CBlockIndex* pindexBlock,
		const CCoins *txindex, int maxDepth) {
	for (CBlockIndex* pindex = pindexBlock;
			pindex && pindexBlock->nHeight - pindex->nHeight < maxDepth;
			pindex = pindex->pprev)
		if (pindex->nHeight == (int) txindex->nHeight)
			return pindexBlock->nHeight - pindex->nHeight;
	return -1;
}

int64 GetTxHashHeight(const uint256 txHash) {
	CDiskTxPos postx;
	pblocktree->ReadTxIndex(txHash, postx);
	return GetTxPosHeight(postx);
}

int64 GetOfferNetFee(const CTransaction& tx) {
	int64 nFee = 0;
	for (unsigned int i = 0; i < tx.vout.size(); i++) {
		const CTxOut& out = tx.vout[i];
		if (out.scriptPubKey.size() == 1 && out.scriptPubKey[0] == OP_RETURN)
			nFee += out.nValue;
	}
	return nFee;
}

int GetOfferHeight(vector<unsigned char> vchOffer) {
	vector<COffer> vtxPos;
	if (pofferdb->ExistsOffer(vchOffer)) {
		if (!pofferdb->ReadOffer(vchOffer, vtxPos))
			return error("GetOfferHeight() : failed to read from offer DB");
		if (vtxPos.empty()) return -1;
		COffer& txPos = vtxPos.back();
		return txPos.nHeight;
	}
	return -1;
}


int IndexOfOfferOutput(const CTransaction& tx) {
	vector<vector<unsigned char> > vvch;
	int op, nOut;
	if (!DecodeOfferTx(tx, op, nOut, vvch, -1))
		throw runtime_error("IndexOfOfferOutput() : offer output not found");
	return nOut;
}

bool GetNameOfOfferTx(const CTransaction& tx, vector<unsigned char>& offer) {
	if (tx.nVersion != SYSCOIN_TX_VERSION)
		return false;
	vector<vector<unsigned char> > vvchArgs;
	int op, nOut;
	if (!DecodeOfferTx(tx, op, nOut, vvchArgs, -1))
		return error("GetNameOfOfferTx() : could not decode a syscoin tx");

	switch (op) {
		case OP_OFFER_ACTIVATE:
		case OP_OFFER_UPDATE:
		case OP_OFFER_ACCEPT:
		case OP_OFFER_REFUND:
			offer = vvchArgs[0];
			return true;
	}
	return false;
}


bool GetValueOfOfferTx(const CTransaction& tx, vector<unsigned char>& value) {
	vector<vector<unsigned char> > vvch;
	int op, nOut;

	if (!DecodeOfferTx(tx, op, nOut, vvch, -1))
		return false;

	switch (op) {
	case OP_OFFER_ACTIVATE:
	case OP_OFFER_ACCEPT:
	case OP_OFFER_UPDATE:
		value = vvch[1];
		return true;
	case OP_OFFER_REFUND:
		value = vvch[2];
		return true;
	default:
		return false;
	}
}

bool IsOfferMine(const CTransaction& tx) {
	if (tx.nVersion != SYSCOIN_TX_VERSION)
		return false;

	vector<vector<unsigned char> > vvch;
	int op, nOut;

	bool good = DecodeOfferTx(tx, op, nOut, vvch, -1);
	if (!good) 
		return false;
	
	if(!IsOfferOp(op))
		return false;

	const CTxOut& txout = tx.vout[nOut];
	if (IsMyOffer(tx, txout)) {
		if(fDebug)
			printf("IsOfferMine() : found my transaction %s nout %d\n",
				tx.GetHash().GetHex().c_str(), nOut);
		return true;
	}
	return false;
}

bool IsOfferMine(const CTransaction& tx, const CTxOut& txout) {
	if (tx.nVersion != SYSCOIN_TX_VERSION)
		return false;

	vector<vector<unsigned char> > vvch;
	int op;

	if (!DecodeOfferScript(txout.scriptPubKey, op, vvch))
		return false;

	if(!IsOfferOp(op))
		return false;


	if (IsMyOffer(tx, txout)) {
		if(fDebug)
			printf("IsOfferMine() : found my transaction %s value %d\n",
				tx.GetHash().GetHex().c_str(), (int) txout.nValue);
		return true;
	}
	return false;
}

bool GetValueOfOfferTxHash(const uint256 &txHash,
		vector<unsigned char>& vchValue, uint256& hash, int& nHeight) {
	nHeight = GetTxHashHeight(txHash);
	CTransaction tx;
	uint256 blockHash;
	if (!GetTransaction(txHash, tx, blockHash, true))
		return error("GetValueOfOfferTxHash() : could not read tx from disk");
	if (!GetValueOfOfferTx(tx, vchValue))
		return error("GetValueOfOfferTxHash() : could not decode value from tx");
	hash = tx.GetHash();
	return true;
}

bool GetValueOfOffer(COfferDB& dbOffer, const vector<unsigned char> &vchOffer,
		vector<unsigned char>& vchValue, int& nHeight) {
	vector<COffer> vtxPos;
	if (!pofferdb->ReadOffer(vchOffer, vtxPos) || vtxPos.empty())
		return false;

	COffer& txPos = vtxPos.back();
	nHeight = txPos.nHeight;
	vchValue = txPos.vchRand;
	return true;
}

bool GetTxOfOffer(COfferDB& dbOffer, const vector<unsigned char> &vchOffer, 
				  COffer& txPos, CTransaction& tx) {
	vector<COffer> vtxPos;
	if (!pofferdb->ReadOffer(vchOffer, vtxPos) || vtxPos.empty())
		return false;
	txPos = vtxPos.back();
	int nHeight = txPos.nHeight;
	if (nHeight + GetOfferExpirationDepth()
			< pindexBest->nHeight) {
		string offer = stringFromVch(vchOffer);
		if(fDebug)
			printf("GetTxOfOffer(%s) : expired", offer.c_str());
		return false;
	}

	uint256 hashBlock;
	if (!GetTransaction(txPos.txHash, tx, hashBlock, true))
		return false;

	return true;
}

bool GetTxOfOfferAccept(COfferDB& dbOffer, const vector<unsigned char> &vchOfferAccept,
		COffer &txPos, CTransaction& tx) {
	vector<COffer> vtxPos;
	vector<unsigned char> vchOffer;
	if (!pofferdb->ReadOfferAccept(vchOfferAccept, vchOffer)) return false;
	if (!pofferdb->ReadOffer(vchOffer, vtxPos)) return false;
	txPos = vtxPos.back();
	int nHeight = txPos.nHeight;
	if (nHeight + GetOfferExpirationDepth()
			< pindexBest->nHeight) {
		string offer = stringFromVch(vchOfferAccept);
		if(fDebug)
			printf("GetTxOfOfferAccept(%s) : expired", offer.c_str());
		return false;
	}

	uint256 hashBlock;
	if (!GetTransaction(txPos.txHash, tx, hashBlock, true))
		return error("GetTxOfOfferAccept() : could not read tx from disk");

	return true;
}

bool DecodeOfferTx(const CTransaction& tx, int& op, int& nOut,
		vector<vector<unsigned char> >& vvch, int nHeight) {
	bool found = false;


	// Strict check - bug disallowed
	for (unsigned int i = 0; i < tx.vout.size(); i++) {
		const CTxOut& out = tx.vout[i];
		vector<vector<unsigned char> > vvchRead;
		if (DecodeOfferScript(out.scriptPubKey, op, vvch)) {
			nOut = i; found = true;
			break;
		}
	}
	if (!found) vvch.clear();
	return found && IsOfferOp(op);
}
bool DecodeOfferTxInputs(const CTransaction& tx, int& op, int& nOut,
		vector<vector<unsigned char> >& vvch, CCoinsViewCache &inputs) {
	bool found = false;

	const COutPoint *prevOutput = NULL;
	const CCoins *prevCoins = NULL;
    // Strict check - bug disallowed
    for (int i = 0; i < (int)tx.vin.size(); i++) {
		prevOutput = &tx.vin[i].prevout;
		prevCoins = &inputs.GetCoins(prevOutput->hash);
        if (DecodeOfferScript(prevCoins->vout[prevOutput->n].scriptPubKey, op, vvch)) {
            nOut = i; found = true; 
            break;
        }
    }
	if (!found) vvch.clear();
	return found && IsOfferOp(op);
}


bool DecodeOfferTx(const CCoins& tx, int& op, int& nOut,
		vector<vector<unsigned char> >& vvch, int nHeight) {
	bool found = false;


	// Strict check - bug disallowed
	for (unsigned int i = 0; i < tx.vout.size(); i++) {
		const CTxOut& out = tx.vout[i];
		vector<vector<unsigned char> > vvchRead;
		if (DecodeOfferScript(out.scriptPubKey, op, vvchRead)) {
			nOut = i; found = true; vvch = vvchRead;
			break;
		}
	}
	if (!found)
		vvch.clear();
	return found;
}

bool DecodeOfferScript(const CScript& script, int& op,
		vector<vector<unsigned char> > &vvch) {
	CScript::const_iterator pc = script.begin();
	return DecodeOfferScript(script, op, vvch, pc);
}

bool DecodeOfferScript(const CScript& script, int& op,
		vector<vector<unsigned char> > &vvch, CScript::const_iterator& pc) {
	opcodetype opcode;
	if (!script.GetOp(pc, opcode)) return false;
	if (opcode < OP_1 || opcode > OP_16) return false;
	op = CScript::DecodeOP_N(opcode);

	for (;;) {
		vector<unsigned char> vch;
		if (!script.GetOp(pc, opcode, vch))
			return false;
		if (opcode == OP_DROP || opcode == OP_2DROP || opcode == OP_NOP)
			break;
		if (!(opcode >= 0 && opcode <= OP_PUSHDATA4))
			return false;
		vvch.push_back(vch);
	}

	// move the pc to after any DROP or NOP
	while (opcode == OP_DROP || opcode == OP_2DROP || opcode == OP_NOP) {
		if (!script.GetOp(pc, opcode))
			break;
	}

	pc--;

	if ((op == OP_OFFER_ACTIVATE && vvch.size() == 2)
		|| (op == OP_OFFER_UPDATE && vvch.size() == 2)
		|| (op == OP_OFFER_ACCEPT && vvch.size() == 2)
		|| (op == OP_OFFER_REFUND && vvch.size() == 3))
		return true;
	return false;
}

bool SignOfferSignature(const CTransaction& txFrom, CTransaction& txTo,
		unsigned int nIn, int nHashType = SIGHASH_ALL, CScript scriptPrereq =
				CScript()) {
	assert(nIn < txTo.vin.size());
	CTxIn& txin = txTo.vin[nIn];
	assert(txin.prevout.n < txFrom.vout.size());
	const CTxOut& txout = txFrom.vout[txin.prevout.n];

	// Leave out the signature from the hash, since a signature can't sign itself.
	// The checksig op will also drop the signatures from its hash.
	const CScript &scriptPubKey = RemoveOfferScriptPrefix(txout.scriptPubKey);
	uint256 hash = SignatureHash(scriptPrereq + txout.scriptPubKey, txTo, nIn,
			nHashType);
	txnouttype whichTypeRet;

	if (!Solver(*pwalletMain, scriptPubKey, hash, nHashType, txin.scriptSig,
			whichTypeRet))
		return false;

	txin.scriptSig = scriptPrereq + txin.scriptSig;

	// Test the solution
	if (scriptPrereq.empty())
		if (!VerifyScript(txin.scriptSig, txout.scriptPubKey, txTo, nIn, 0, 0))
			return false;

	return true;
}

bool CreateOfferTransactionWithInputTx(
		const vector<pair<CScript, int64> >& vecSend, CWalletTx& wtxIn,
		int nTxOut, CWalletTx& wtxNew, CReserveKey& reservekey, int64& nFeeRet,
		const string& txData) {
	int64 nValue = 0;
	BOOST_FOREACH(const PAIRTYPE(CScript, int64)& s, vecSend) {
		if (nValue < 0)
			return false;
		nValue += s.second;
	}
	if (vecSend.empty() || nValue < 0)
		return false;

	wtxNew.BindWallet(pwalletMain);

	nFeeRet = nTransactionFee;
	while(true) {
		wtxNew.vin.clear();
		wtxNew.vout.clear();
		wtxNew.fFromMe = true;
		wtxNew.data = vchFromString(txData);

		int64 nTotalValue = nValue + nFeeRet;
		if(fDebug)
			printf("CreateOfferTransactionWithInputTx: total value = %d\n",
				(int) nTotalValue);
		double dPriority = 0;

		// vouts to the payees
		BOOST_FOREACH(const PAIRTYPE(CScript, int64)& s, vecSend)
			wtxNew.vout.push_back(CTxOut(s.second, s.first));

		int64 nWtxinCredit = wtxIn.vout[nTxOut].nValue;

		// Choose coins to use
		set<pair<const CWalletTx*, unsigned int> > setCoins;
		int64 nValueIn = 0;
		if(fDebug)
			printf( "CreateOfferTransactionWithInputTx: SelectCoins(%s), nTotalValue = %s, nWtxinCredit = %s\n",
				FormatMoney(nTotalValue - nWtxinCredit).c_str(),
				FormatMoney(nTotalValue).c_str(),
				FormatMoney(nWtxinCredit).c_str());
		if (nTotalValue - nWtxinCredit > 0) {
			if (!pwalletMain->SelectCoins(nTotalValue - nWtxinCredit,
					setCoins, nValueIn))
				return false;
		}
		if(fDebug)
			printf( "CreateOfferTransactionWithInputTx: selected %d tx outs, nValueIn = %s\n",
				(int) setCoins.size(), FormatMoney(nValueIn).c_str());

		vector<pair<const CWalletTx*, unsigned int> > vecCoins(
				setCoins.begin(), setCoins.end());

		BOOST_FOREACH(PAIRTYPE(const CWalletTx*, unsigned int)& coin, vecCoins) {
			int64 nCredit = coin.first->vout[coin.second].nValue;
			dPriority += (double) nCredit
					* coin.first->GetDepthInMainChain();
		}

		// Input tx always at first position
		vecCoins.insert(vecCoins.begin(), make_pair(&wtxIn, nTxOut));

		nValueIn += nWtxinCredit;
		dPriority += (double) nWtxinCredit * wtxIn.GetDepthInMainChain();

		// Fill a vout back to self with any change
		int64 nChange = nValueIn - nTotalValue;
		if (nChange >= CENT) {
			// Note: We use a new key here to keep it from being obvious which side is the change.
			//  The drawback is that by not reusing a previous key, the change may be lost if a
			//  backup is restored, if the backup doesn't have the new private key for the change.
			//  If we reused the old key, it would be possible to add code to look for and
			//  rediscover unknown transactions that were written with keys of ours to recover
			//  post-backup change.

			// Reserve a new key pair from key pool
			CPubKey pubkey;
			assert(reservekey.GetReservedKey(pubkey));

			// -------------- Fill a vout to ourself, using same address type as the payment
			// Now sending always to hash160 (GetBitcoinAddressHash160 will return hash160, even if pubkey is used)
			CScript scriptChange;
			if (Hash160(vecSend[0].first) != 0)
				scriptChange.SetDestination(pubkey.GetID());
			else
				scriptChange << pubkey << OP_CHECKSIG;

			// Insert change txn at random position:
			vector<CTxOut>::iterator position = wtxNew.vout.begin()
					+ GetRandInt(wtxNew.vout.size());
			wtxNew.vout.insert(position, CTxOut(nChange, scriptChange));
		} else
			reservekey.ReturnKey();

		// Fill vin
		BOOST_FOREACH(PAIRTYPE(const CWalletTx*, unsigned int)& coin, vecCoins)
			wtxNew.vin.push_back(CTxIn(coin.first->GetHash(), coin.second));

		// Sign
		int nIn = 0;
		BOOST_FOREACH(PAIRTYPE(const CWalletTx*, unsigned int)& coin, vecCoins) {
			if (coin.first == &wtxIn
					&& coin.second == (unsigned int) nTxOut) {
				if (!SignOfferSignature(*coin.first, wtxNew, nIn++))
					throw runtime_error("could not sign offer coin output");

			} else {
				if (!SignSignature(*pwalletMain, *coin.first, wtxNew, nIn++))
					return false;
			}
		}

		// Limit size
		unsigned int nBytes = ::GetSerializeSize(*(CTransaction*) &wtxNew,
				SER_NETWORK, PROTOCOL_VERSION);
		if (nBytes >= MAX_BLOCK_SIZE_GEN / 5)
			return false;
		dPriority /= nBytes;

		// Check that enough fee is included
		int64 nPayFee = nTransactionFee * (1 + (int64) nBytes / 1000);
		bool fAllowFree = CTransaction::AllowFree(dPriority);
		int64 nMinFee = wtxNew.GetMinFee(1, fAllowFree);
		if (nFeeRet < max(nPayFee, nMinFee)) {
			nFeeRet = max(nPayFee, nMinFee);
			if(fDebug)
				printf( "CreateOfferTransactionWithInputTx: re-iterating (nFreeRet = %s)\n",
					FormatMoney(nFeeRet).c_str());
			continue;
		}

		// Fill vtxPrev by copying from previous transactions vtxPrev
		wtxNew.AddSupportingTransactions();
		wtxNew.fTimeReceivedIsTxTime = true;

		break;
	}
	
	if(fDebug)
		printf("CreateOfferTransactionWithInputTx succeeded:\n%s",
			wtxNew.ToString().c_str());
	return true;
}

int64 GetFeeAssign() {
	int64 iRet = !0;
	return  iRet<<47;
}
void EraseOffer(CWalletTx& wtx)
{
	UnspendInputs(wtx);
 	wtx.RemoveFromMemoryPool();
 	pwalletMain->EraseFromWallet(wtx.GetHash());

}

// nTxOut is the output from wtxIn that we should grab
string SendOfferMoneyWithInputTx(CScript scriptPubKey, int64 nValue,
		int64 nNetFee, CWalletTx& wtxIn, CWalletTx& wtxNew, bool fAskFee,
		const string& txData) {
	int nTxOut = IndexOfOfferOutput(wtxIn);
	CReserveKey reservekey(pwalletMain);
	int64 nFeeRequired;
	vector<pair<CScript, int64> > vecSend;
	vecSend.push_back(make_pair(scriptPubKey, nValue));

	if (nNetFee) {
		CScript scriptFee;
		scriptFee << OP_RETURN;
		vecSend.push_back(make_pair(scriptFee, nNetFee));
	}

	if (!CreateOfferTransactionWithInputTx(vecSend, wtxIn, nTxOut, wtxNew,
			reservekey, nFeeRequired, txData)) {
		string strError;
		if (nValue + nFeeRequired > pwalletMain->GetBalance())
			strError = strprintf(_("Error: This transaction requires a transaction fee of at least %s because of its amount, complexity, or use of recently received funds "),
							FormatMoney(nFeeRequired).c_str());
		else
			strError = _("Error: Transaction creation failed, not enough balance.");
		if(fDebug)
			printf("SendMoney() : %s", strError.c_str());
		return strError;
	}

#ifdef GUI
	if (fAskFee && !uiInterface.ThreadSafeAskFee(nFeeRequired))
	return "ABORTED";
#else
	if (fAskFee && !true)
		return "ABORTED";
#endif

	if (!pwalletMain->CommitTransaction(wtxNew, reservekey))
	{
		return _(
				"Error: The transaction was rejected.");
	}
	return "";
}

bool GetOfferAddress(const CTransaction& tx, std::string& strAddress) {
	int op, nOut = 0;
	vector<vector<unsigned char> > vvch;

	if (!DecodeOfferTx(tx, op, nOut, vvch, -1))
		return error("GetOfferAddress() : could not decode offer tx.");

	const CTxOut& txout = tx.vout[nOut];

	const CScript& scriptPubKey = RemoveOfferScriptPrefix(txout.scriptPubKey);
	CTxDestination dest;
	ExtractDestination(scriptPubKey, dest);
	strAddress = CBitcoinAddress(dest).ToString();
	return true;
}

bool GetOfferAddress(const CDiskTxPos& txPos, std::string& strAddress) {
	CTransaction tx;
	if (!tx.ReadFromDisk(txPos))
		return error("GetOfferAddress() : could not read tx from disk");
	return GetOfferAddress(tx, strAddress);
}

CScript RemoveOfferScriptPrefix(const CScript& scriptIn) {
	int op;
	vector<vector<unsigned char> > vvch;
	CScript::const_iterator pc = scriptIn.begin();
	
	if (!DecodeOfferScript(scriptIn, op, vvch, pc))
	{
		throw runtime_error(
			"RemoveOfferScriptPrefix() : could not decode offer script");
	}

	return CScript(pc, scriptIn.end());
}

bool CheckOfferInputs(CBlockIndex *pindexBlock, const CTransaction &tx,
		CValidationState &state, CCoinsViewCache &inputs, bool fBlock, bool fMiner,
		bool fJustCheck) {
	if (!tx.IsCoinBase()) {
		if (fDebug)
			printf("*** %d %d %s %s %s %s\n", pindexBlock->nHeight,
				pindexBest->nHeight, tx.GetHash().ToString().c_str(),
				fBlock ? "BLOCK" : "", fMiner ? "MINER" : "",
				fJustCheck ? "JUSTCHECK" : "");

		bool found = false;
		const COutPoint *prevOutput = NULL;
		const CCoins *prevCoins = NULL;
		int prevOp;
		vector<vector<unsigned char> > vvchPrevArgs;
		vvchPrevArgs.clear();
		int nIn = -1;
		// Strict check - bug disallowed
		for (int i = 0; i < (int) tx.vin.size(); i++) {
			prevOutput = &tx.vin[i].prevout;
			prevCoins = &inputs.GetCoins(prevOutput->hash);
			vector<vector<unsigned char> > vvch, vvch2;
			if (DecodeOfferScript(prevCoins->vout[prevOutput->n].scriptPubKey, prevOp, vvch)) {
				found = true; 
				vvchPrevArgs = vvch;
				break;
			}
			else if (DecodeCertScript(prevCoins->vout[prevOutput->n].scriptPubKey, prevOp, vvch2))
			{
				found = true; 
				vvchPrevArgs = vvch2;
				break;
			}
			if(!found)vvchPrevArgs.clear();
		}

		// Make sure offer outputs are not spent by a regular transaction, or the offer would be lost
		if (tx.nVersion != SYSCOIN_TX_VERSION) {
			if (found)
				return error(
						"CheckOfferInputs() : a non-syscoin transaction with a syscoin input");
			return true;
		}

		vector<vector<unsigned char> > vvchArgs;
		int op;
		int nOut;
		bool good = DecodeOfferTx(tx, op, nOut, vvchArgs, -1);
		if (!good)
			return error("CheckOfferInputs() : could not decode a syscoin tx");
		int nDepth;
		int64 nNetFee;

		// unserialize offer object from txn, check for valid
		COffer theOffer(tx);
		COfferAccept theOfferAccept;
		if (theOffer.IsNull())
			error("CheckOfferInputs() : null offer object");
		if(theOffer.sDescription.size() > 1024 * 64)
		{
			return error("offer description too big");
		}
		if(theOffer.sTitle.size() > MAX_NAME_LENGTH)
		{
			return error("offer title too big");
		}
		if(theOffer.sCategory.size() > MAX_NAME_LENGTH)
		{
			return error("offer category too big");
		}
		if(theOffer.vchRand.size() > 20)
		{
			return error("offer rand too big");
		}
		if(theOffer.vchLinkOffer.size() > MAX_NAME_LENGTH)
		{
			return error("offer link guid too big");
		}
		if(theOffer.vchPubKey.size() > MAX_NAME_LENGTH)
		{
			return error("offer pub key too big");
		}
		if(theOffer.sCurrencyCode.size() > MAX_NAME_LENGTH)
		{
			return error("offer currency code too big");
		}
		if (vvchArgs[0].size() > MAX_NAME_LENGTH)
			return error("offer hex guid too long");

		switch (op) {
		case OP_OFFER_ACTIVATE:
			if ( found && ( prevOp != OP_CERT_ACTIVATE && prevOp != OP_CERT_UPDATE  ) )
				return error("offeractivate previous op is invalid");		
			if (found && vvchPrevArgs[0] != vvchArgs[0])
				return error("CheckOfferInputs() : offernew cert mismatch");
			if (vvchArgs[1].size() > 20)
				return error("offeractivate tx with guid too big");

			if (fBlock && !fJustCheck) {

					// check for enough fees
				nNetFee = GetOfferNetFee(tx);
				if (nNetFee < GetOfferNetworkFee(OP_OFFER_ACTIVATE, theOffer.nHeight))
					return error(
							"CheckOfferInputs() : OP_OFFER_ACTIVATE got tx %s with fee too low %lu",
							tx.GetHash().GetHex().c_str(),
							(long unsigned int) nNetFee);		
			}
			break;


		case OP_OFFER_UPDATE:
			if ( !found || ( prevOp != OP_OFFER_ACTIVATE && prevOp != OP_OFFER_UPDATE 
				&& prevOp != OP_OFFER_REFUND ) )
				return error("offerupdate previous op %s is invalid", offerFromOp(prevOp).c_str());
			
			if (vvchArgs[1].size() > MAX_VALUE_LENGTH)
				return error("offerupdate tx with value too long");
			
			if (vvchPrevArgs[0] != vvchArgs[0])
				return error("CheckOfferInputs() : offerupdate offer mismatch");
			if (fBlock && !fJustCheck) {
				if(vvchPrevArgs.size() > 0)
				{
					nDepth = CheckTransactionAtRelativeDepth(pindexBlock,
							prevCoins, GetCertExpirationDepth());
					if ((fBlock || fMiner) && nDepth < 0)
						return error(
								"CheckOfferInputs() : offerupdate on an expired offer, or there is a pending transaction on the offer");	
	  
				}
				else
				{
					nDepth = CheckTransactionAtRelativeDepth(pindexBlock,
							prevCoins, GetOfferExpirationDepth());
					if ((fBlock || fMiner) && nDepth < 0)
						return error(
								"CheckOfferInputs() : offerupdate on an expired cert previous tx, or there is a pending transaction on the cert");		  
				}

				// TODO CPU intensive
				nDepth = CheckTransactionAtRelativeDepth(pindexBlock,
						prevCoins, GetOfferExpirationDepth());
				if ((fBlock || fMiner) && nDepth < 0)
					return error(
							"CheckOfferInputs() : offerupdate on an expired offer, or there is a pending transaction on the offer");
					// check for enough fees
				nNetFee = GetOfferNetFee(tx);
				if (nNetFee < GetOfferNetworkFee(OP_OFFER_UPDATE, theOffer.nHeight))
					return error(
							"CheckOfferInputs() : OP_OFFER_UPDATE got tx %s with fee too low %lu",
							tx.GetHash().GetHex().c_str(),
							(long unsigned int) nNetFee);		
			}
			break;
		case OP_OFFER_REFUND:
			int nDepth;
			if ( !found || ( prevOp != OP_OFFER_ACTIVATE && prevOp != OP_OFFER_UPDATE && prevOp != OP_OFFER_REFUND ))
				return error("offerrefund previous op %s is invalid", offerFromOp(prevOp).c_str());		
			if(op == OP_OFFER_REFUND && vvchArgs[2] == OFFER_REFUND_COMPLETE && vvchPrevArgs[2] != OFFER_REFUND_PAYMENT_INPROGRESS)
				return error("offerrefund complete tx must be linked to an inprogress tx");
			
			if (vvchArgs[1].size() > 20)
				return error("offerrefund tx with guid too big");
			if (vvchArgs[2].size() > 20)
				return error("offerrefund refund status too long");
			if (vvchPrevArgs[0] != vvchArgs[0])
				return error("CheckOfferInputs() : offerrefund offer mismatch");
			if (fBlock && !fJustCheck) {
				// Check hash
				const vector<unsigned char> &vchAcceptRand = vvchArgs[1];
				
				// check for existence of offeraccept in txn offer obj
				if(!theOffer.GetAcceptByHash(vchAcceptRand, theOfferAccept))
					return error("OP_OFFER_REFUND could not read accept from offer txn");
				// TODO CPU intensive
				nDepth = CheckTransactionAtRelativeDepth(pindexBlock,
						prevCoins, GetOfferExpirationDepth());
				if ((fBlock || fMiner) && nDepth < 0)
					return error(
							"CheckOfferInputs() : offerrefund on an expired offer, or there is a pending transaction on the offer");
		
			}
			break;
		case OP_OFFER_ACCEPT:
			if (found && !IsCertOp(prevOp))
				return error("CheckOfferInputs() : offeraccept cert mismatch");
			if (vvchArgs[1].size() > 20)
				return error("offeraccept tx with guid too big");

			if (fBlock && !fJustCheck) {
				// Check hash
				const vector<unsigned char> &vchAcceptRand = vvchArgs[1];
				
				// check for existence of offeraccept in txn offer obj
				if(!theOffer.GetAcceptByHash(vchAcceptRand, theOfferAccept))
					return error("OP_OFFER_ACCEPT could not read accept from offer txn");
				if(found && theOfferAccept.vchCertLink != vvchPrevArgs[0])
				{
					return error("theOfferAccept.vchCertlink and vvchPrevArgs[0] don't match");
				}
	   		}
			break;

		default:
			return error( "CheckOfferInputs() : offer transaction has unknown op");
		}

		
		if (fBlock || (!fBlock && !fMiner && !fJustCheck)) {
			// save serialized offer for later use
			COffer serializedOffer = theOffer;
			COffer linkOffer;
			// load the offer data from the DB
			vector<COffer> vtxPos;
			if (pofferdb->ExistsOffer(vvchArgs[0]) && !fJustCheck) {
				if (!pofferdb->ReadOffer(vvchArgs[0], vtxPos))
					return error(
							"CheckOfferInputs() : failed to read from offer DB");
			}

			if (!fMiner && !fJustCheck && pindexBlock->nHeight != pindexBest->nHeight) {
				int nHeight = pindexBlock->nHeight;
				// get the latest offer from the db
            	theOffer.nHeight = nHeight;
            	theOffer.GetOfferFromList(vtxPos);

				// If update, we make the serialized offer the master
				// but first we assign the accepts from the DB since
				// they are not shipped in an update txn to keep size down
				if(op == OP_OFFER_UPDATE) {
					serializedOffer.accepts = theOffer.accepts;
					theOffer = serializedOffer;
				}
				else if(op == OP_OFFER_ACTIVATE)
				{
					// if this is a linked offer activate, then add it to the parent offerLinks list
					if(!theOffer.vchLinkOffer.empty())
					{
						vector<COffer> myVtxPos;
						if (pofferdb->ExistsOffer(theOffer.vchLinkOffer)) {
							if (pofferdb->ReadOffer(theOffer.vchLinkOffer, myVtxPos))
							{
								COffer myParentOffer = myVtxPos.back();
								myParentOffer.offerLinks.push_back(vvchArgs[0]);							
								myParentOffer.PutToOfferList(myVtxPos);
								{
								TRY_LOCK(cs_main, cs_trymain);
								// write parent offer
								if (!pofferdb->WriteOffer(theOffer.vchLinkOffer, myVtxPos))
									return error( "CheckOfferInputs() : failed to write to offer link to DB");
								}
							}
						}
						
					}
				}
				else if(op == OP_OFFER_REFUND)
				{
					serializedOffer.accepts = theOffer.accepts;
					theOffer = serializedOffer;
					vector<unsigned char> vchOfferAccept = vvchArgs[1];
					if (pofferdb->ExistsOfferAccept(vchOfferAccept)) {
						if (!pofferdb->ReadOfferAccept(vchOfferAccept, vvchArgs[0]))
						{
							return error("CheckOfferInputs()- OP_OFFER_REFUND: failed to read offer accept from offer DB\n");
						}

					}

					if(!theOffer.GetAcceptByHash(vchOfferAccept, theOfferAccept))
						return error("CheckOfferInputs()- OP_OFFER_REFUND: could not read accept from serializedOffer txn");	
            		
					if(!fInit && pwalletMain && vvchArgs[2] == OFFER_REFUND_PAYMENT_INPROGRESS){
						CTransaction offerTx;
						COffer tmpOffer;
						if(!GetTxOfOffer(*pofferdb, vvchArgs[0], tmpOffer, offerTx))	
						{
							return error("CheckOfferInputs() - OP_OFFER_REFUND: failed to get offer transaction");
						}
						// we want to refund my offer accepts and any linked offeraccepts of my offer						
						if(IsOfferMine(offerTx))
						{
							string strError = makeOfferRefundTX(tx, theOffer, theOfferAccept, OFFER_REFUND_COMPLETE);
							if (strError != "" && fDebug)
							{
								printf("CheckOfferInputs() - OFFER_REFUND_COMPLETE %s\n", strError.c_str());
							}

						}

						CTransaction myOfferTx;
						COffer activateOffer;
						COfferAccept myAccept;
						// if this accept was done via offer linking (makeOfferLinkAcceptTX) then walk back up and refund
						if(GetTxOfOfferAccept(*pofferdb, theOfferAccept.vchLinkOfferAccept, activateOffer, myOfferTx))
						{
							if(!activateOffer.GetAcceptByHash(theOfferAccept.vchLinkOfferAccept, myAccept))
								return error("CheckOfferInputs()- OFFER_REFUND_PAYMENT_INPROGRESS: could not read accept from offer txn");	
							if(IsOfferMine(myOfferTx))
							{
								string strError = makeOfferRefundTX(myOfferTx, activateOffer, myAccept, OFFER_REFUND_PAYMENT_INPROGRESS);
								if (strError != "" && fDebug)							
									printf("CheckOfferInputs() - OFFER_REFUND_PAYMENT_INPROGRESS %s\n", strError.c_str());
									
							}
						}
					}
					else if(vvchArgs[2] == OFFER_REFUND_COMPLETE){
						theOfferAccept.bRefunded = true;
						theOfferAccept.txRefundId = tx.GetHash();
					}
					theOffer.PutOfferAccept(theOfferAccept);
					{
					TRY_LOCK(cs_main, cs_trymain);
					// write the offer / offer accept mapping to the database
					if (!pofferdb->WriteOfferAccept(vvchArgs[1], vvchArgs[0]))
						return error( "CheckOfferInputs() : failed to write to offer DB");
					}
					
				}
				else if (op == OP_OFFER_ACCEPT) {
					
					COffer myOffer,linkOffer;
					CTransaction offerTx, linkedTx;			
					// find the payment from the tx outputs (make sure right amount of coins were paid for this offer accept), the payment amount found has to be exact
					if(tx.vout.size() > 1)
					{	
						bool foundPayment = false;
						COfferLinkWhitelistEntry entry;
						if(IsCertOp(prevOp) && found)
						{	
							theOffer.linkWhitelist.GetLinkEntryByHash(theOfferAccept.vchCertLink, entry);						
						}

						int precision = 2;
						// lookup the price of the offer in syscoin based on pegged alias at the block # when accept was made (sets nHeight in offeraccept serialized object in tx)
						int64 nPrice = convertCurrencyCodeToSyscoin(theOffer.sCurrencyCode, theOffer.GetPrice(entry), theOfferAccept.nHeight, precision)*theOfferAccept.nQty;
						for(unsigned int i=0;i<tx.vout.size();i++)
						{
							if(tx.vout[i].nValue == nPrice)
							{
								foundPayment = true;
								break;
							}
						}
						if(!foundPayment)
						{
							if(fDebug)
								printf("CheckOfferInputs() OP_OFFER_ACCEPT: this offer accept does not pay enough according to the offer price %llu\n", nPrice);
							return true;
						}
					}
					else
					{
						if(fDebug)
							printf("CheckOfferInputs() OP_OFFER_ACCEPT: offer payment not found in accept tx\n");
						return true;
					}
					if (!GetTxOfOffer(*pofferdb, vvchArgs[0], myOffer, offerTx))
						return error("CheckOfferInputs() OP_OFFER_ACCEPT: could not find an offer with this name");

					if(!myOffer.vchLinkOffer.empty())
					{
						if(!GetTxOfOffer(*pofferdb, myOffer.vchLinkOffer, linkOffer, linkedTx))
							linkOffer.SetNull();
					}
											
					// check for existence of offeraccept in txn offer obj
					if(!serializedOffer.GetAcceptByHash(vvchArgs[1], theOfferAccept))
						return error("CheckOfferInputs() OP_OFFER_ACCEPT: could not read accept from offer txn");					


					// 2 step refund: send an offer accept with nRefunded property set to inprogress and then send another with complete later
					// first step is to send inprogress so that next block we can send a complete (also sends coins during second step to original acceptor)
					// this is to ensure that the coins sent during accept are available to spend to refund to avoid having to hold double the balance of an accept amount
					// in order to refund.
					if(theOfferAccept.nQty <= 0 || (theOfferAccept.nQty > theOffer.nQty || (!linkOffer.IsNull() && theOfferAccept.nQty > linkOffer.nQty))) {

						if(pwalletMain && IsOfferMine(offerTx) && theOfferAccept.nQty > 0)
						{	
							string strError = makeOfferRefundTX(offerTx, theOffer, theOfferAccept, OFFER_REFUND_PAYMENT_INPROGRESS);
							if (strError != "" && fDebug)
								printf("CheckOfferInputs() - OP_OFFER_ACCEPT %s\n", strError.c_str());
							
						}
						if(fDebug)
							printf("txn %s accepted but offer not fulfilled because desired"
							" qty %u is more than available qty %u for offer accept %s\n", 
							tx.GetHash().GetHex().c_str(), 
							theOfferAccept.nQty, 
							theOffer.nQty, 
							HexStr(theOfferAccept.vchRand).c_str());
						return true;
					}
					if(theOffer.vchLinkOffer.empty())
					{
						theOffer.nQty -= theOfferAccept.nQty;
						// go through the linked offers, if any, and update the linked offer qty based on the this qty
						for(unsigned int i=0;i<theOffer.offerLinks.size();i++) {
							vector<COffer> myVtxPos;
							if (pofferdb->ExistsOffer(theOffer.offerLinks[i])) {
								if (pofferdb->ReadOffer(theOffer.offerLinks[i], myVtxPos))
								{
									COffer myLinkOffer = myVtxPos.back();
									myLinkOffer.nQty = theOffer.nQty;	
									myLinkOffer.PutToOfferList(myVtxPos);
									{
									TRY_LOCK(cs_main, cs_trymain);
									// write offer
									if (!pofferdb->WriteOffer(theOffer.offerLinks[i], myVtxPos))
										return error( "CheckOfferInputs() : failed to write to offer link to DB");
									}
								}
							}
						}
					}
					if (!fInit && pwalletMain && !linkOffer.IsNull() && IsOfferMine(offerTx))
					{	
						// myOffer.vchLinkOffer is the linked offer guid
						// vvchArgs[1] is this offer accept rand used to walk back up and refund offers in the linked chain
						// we are now accepting the linked	 offer, up the link offer stack.
						string strError = makeOfferLinkAcceptTX(theOfferAccept, myOffer.vchLinkOffer, vvchArgs[1]);
						if(strError != "")
						{
							if(fDebug)
							{
								printf("CheckOfferInputs() - OP_OFFER_ACCEPT - makeOfferLinkAcceptTX %s\n", strError.c_str());
							}
							// if there is a problem refund this accept
							strError = makeOfferRefundTX(offerTx, theOffer, theOfferAccept, OFFER_REFUND_PAYMENT_INPROGRESS);
							if (strError != "" && fDebug)
								printf("CheckOfferInputs() - OP_OFFER_ACCEPT - makeOfferLinkAcceptTX(makeOfferRefundTX) %s\n", strError.c_str());

						}
					}
					
					
					theOfferAccept.bPaid = true;
					
				
					// set the offer accept txn-dependent values and add to the txn
					theOfferAccept.vchRand = vvchArgs[1];
					theOfferAccept.txHash = tx.GetHash();
					theOfferAccept.nTime = pindexBlock->nTime;
					theOfferAccept.nHeight = nHeight;
					theOffer.PutOfferAccept(theOfferAccept);
					{
					TRY_LOCK(cs_main, cs_trymain);
					// write the offer / offer accept mapping to the database
					if (!pofferdb->WriteOfferAccept(vvchArgs[1], vvchArgs[0]))
						return error( "CheckOfferInputs() : failed to write to offer DB");
					}
				}
				
				// only modify the offer's height on an activate or update or refund
				if(op == OP_OFFER_ACTIVATE || op == OP_OFFER_UPDATE ||  op == OP_OFFER_REFUND) {
					theOffer.nHeight = pindexBlock->nHeight;					
					theOffer.txHash = tx.GetHash();
					if(op == OP_OFFER_UPDATE)
					{
						// if this offer is linked to a parent update it with parent information
						if(!theOffer.vchLinkOffer.empty())
						{
							vector<COffer> myVtxPos;
							if (pofferdb->ExistsOffer(theOffer.vchLinkOffer)) {
								if (pofferdb->ReadOffer(theOffer.vchLinkOffer, myVtxPos))
								{
									COffer myLinkOffer = myVtxPos.back();
									theOffer.nQty = myLinkOffer.nQty;	
									theOffer.nHeight = myLinkOffer.nHeight;
									theOffer.SetPrice(myLinkOffer.nPrice);
									
								}
							}
								
						}
						else
						{
							// go through the linked offers, if any, and update the linked offer info based on the this info
							for(unsigned int i=0;i<theOffer.offerLinks.size();i++) {
								vector<COffer> myVtxPos;
								if (pofferdb->ExistsOffer(theOffer.offerLinks[i])) {
									if (pofferdb->ReadOffer(theOffer.offerLinks[i], myVtxPos))
									{
										COffer myLinkOffer = myVtxPos.back();
										myLinkOffer.nQty = theOffer.nQty;	
										myLinkOffer.nHeight = theOffer.nHeight;
										myLinkOffer.SetPrice(theOffer.nPrice);
										myLinkOffer.PutToOfferList(myVtxPos);
										{
										TRY_LOCK(cs_main, cs_trymain);
										// write offer
										if (!pofferdb->WriteOffer(theOffer.offerLinks[i], myVtxPos))
											return error( "CheckOfferInputs() : failed to write to offer link to DB");
										}
									}
								}
							}
							
						}
					}
				}
				// set the offer's txn-dependent values
                theOffer.vchRand = vvchArgs[0];
				theOffer.nTime = pindexBlock->nTime;
				theOffer.PutToOfferList(vtxPos);
				{
				TRY_LOCK(cs_main, cs_trymain);
				// write offer
				if (!pofferdb->WriteOffer(vvchArgs[0], vtxPos))
					return error( "CheckOfferInputs() : failed to write to offer DB");

               				
				// debug
				if (fDebug)
					printf( "CONNECTED OFFER: op=%s offer=%s title=%s qty=%u hash=%s height=%d\n",
						offerFromOp(op).c_str(),
						stringFromVch(vvchArgs[0]).c_str(),
						stringFromVch(theOffer.sTitle).c_str(),
						theOffer.nQty,
						tx.GetHash().ToString().c_str(), 
						nHeight);
				}
			}
		}
	}
	return true;
}

bool ExtractOfferAddress(const CScript& script, string& address) {
	if (script.size() == 1 && script[0] == OP_RETURN) {
		address = string("network fee");
		return true;
	}
	vector<vector<unsigned char> > vvch;
	int op;
	if (!DecodeOfferScript(script, op, vvch))
		return false;

	string strOp = offerFromOp(op);
	string strOffer;

	strOffer = stringFromVch(vvch[0]);

	address = strOp + ": " + strOffer;
	return true;
}

void rescanforoffers(CBlockIndex *pindexRescan) {
    printf("Scanning blockchain for offers to create fast index...\n");
    pofferdb->ReconstructOfferIndex(pindexRescan);
}

int GetTxPosHeight(const CDiskTxPos& txPos) {
    // Read block header
    CBlock block;
    if (!block.ReadFromDisk(txPos)) return 0;
    // Find the block in the index
    map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.find(block.GetHash());
    if (mi == mapBlockIndex.end()) return 0;
    CBlockIndex* pindex = (*mi).second;
    if (!pindex || !pindex->IsInMainChain()) return 0;
    return pindex->nHeight;
}

int GetTxPosHeight2(const CDiskTxPos& txPos, int nHeight) {
    nHeight = GetTxPosHeight(txPos);
    return nHeight;
}

Value getofferfees(const Array& params, bool fHelp) {
	if (fHelp || 0 != params.size())
		throw runtime_error(
				"getaliasfees\n"
						"get current service fees for alias transactions\n");
	Object oRes;
	oRes.push_back(Pair("height", nBestHeight ));
	oRes.push_back(Pair("activate_fee", ValueFromAmount(GetOfferNetworkFee(OP_OFFER_ACTIVATE, nBestHeight) )));
	oRes.push_back(Pair("update_fee", ValueFromAmount(GetOfferNetworkFee(OP_OFFER_UPDATE, nBestHeight) )));
	return oRes;

}

Value offernew(const Array& params, bool fHelp) {
	if (fHelp || params.size() < 6 || params.size() > 6)
		throw runtime_error(
		"offernew <category> <title> <quantity> <price> <description> <currency> [exclusive resell=1]\n"
						"<category> category, 255 chars max.\n"
						"<title> title, 255 chars max.\n"
						"<quantity> quantity, > 0\n"
						"<price> price in <currency>, > 0\n"
						"<description> description, 64 KB max.\n"
						"<currency> The currency code that you want your offer to be in ie: USD.\n"
						"<exclusive resell> set to 1 if you only want those who control the whitelist certificates to be able to resell this offer via offerlink. Defaults to 1.\n"
						+ HelpRequiringPassphrase());
	if(!HasReachedMainNetForkB2())
		throw runtime_error("Please wait until B2 hardfork starts in before executing this command.");
	// gather inputs
	string baSig;
	unsigned int nQty;
	float nPrice;
	bool bExclusiveResell = true;
	vector<unsigned char> vchCat = vchFromValue(params[0]);
	vector<unsigned char> vchTitle = vchFromValue(params[1]);
	vector<unsigned char> vchCurrency = vchFromValue(params[5]);
	vector<unsigned char> vchDesc;
	int qty = atoi(params[2].get_str().c_str());
	if(qty < 0)
	{
		throw runtime_error("invalid quantity value.");
	}
	nQty = (unsigned int)qty;
	nPrice = atof(params[3].get_str().c_str());
	if(nPrice <= 0)
	{
		throw JSONRPCError(RPC_INVALID_PARAMETER, "offer price must be greater than 0!");
	}
	vchDesc = vchFromValue(params[4]);
	if(vchCat.size() < 1)
        throw runtime_error("offer category cannot be empty!");
	if(vchTitle.size() < 1)
        throw runtime_error("offer title cannot be empty!");
	if(vchCat.size() > 255)
        throw runtime_error("offer category cannot exceed 255 bytes!");
	if(vchTitle.size() > 255)
        throw runtime_error("offer title cannot exceed 255 bytes!");
    // 64Kbyte offer desc. maxlen
	if (vchDesc.size() > 1024 * 64)
		throw JSONRPCError(RPC_INVALID_PARAMETER, "offer description cannot exceed 65536 bytes!");

	if(params.size() >= 7)
	{
		bExclusiveResell = atoi(params[6].get_str().c_str()) == 1? true: false;
	}

	
	int64 nRate;
	vector<string> rateList;
	int precision;
	if(getCurrencyToSYSFromAlias(vchCurrency, nRate, nBestHeight, rateList,precision) != "")
	{
		throw JSONRPCError(RPC_INVALID_PARAMETER, "Could not find this currency code in the SYS_RATES alias!\n");
	}
	float minPrice = 1/pow(10,precision);
	float price = nPrice;
	if(price < minPrice)
		price = minPrice;
	string priceStr = strprintf("%.*f", precision, price);
	nPrice = (float)atof(priceStr.c_str());
	// this is a syscoin transaction
	CWalletTx wtx;
	wtx.nVersion = SYSCOIN_TX_VERSION;


	// generate rand identifier
	uint64 rand = GetRand((uint64) -1);
	vector<unsigned char> vchRand = CBigNum(rand).getvch();
	vector<unsigned char> vchOffer = vchFromString(HexStr(vchRand));

	EnsureWalletIsUnlocked();


  	CPubKey newDefaultKey;
	CScript scriptPubKeyOrig;
	pwalletMain->GetKeyFromPool(newDefaultKey, false); 
	std::vector<unsigned char> vchPubKey(newDefaultKey.begin(), newDefaultKey.end());
	string strPubKey = HexStr(vchPubKey);
	// calculate network fees
	int64 nNetFee = GetOfferNetworkFee(OP_OFFER_ACTIVATE, nBestHeight);	
	// unserialize offer object from txn, serialize back
	// build offer object
	COffer newOffer;
	newOffer.vchPubKey = vchFromString(strPubKey);
	newOffer.vchRand = vchOffer;
	newOffer.sCategory = vchCat;
	newOffer.sTitle = vchTitle;
	newOffer.sDescription = vchDesc;
	newOffer.nQty = nQty;
	newOffer.nHeight = nBestHeight;
	newOffer.SetPrice(nPrice);
	newOffer.linkWhitelist.bExclusiveResell = bExclusiveResell;
	newOffer.sCurrencyCode = vchCurrency;
	string bdata = newOffer.SerializeToString();
	
	scriptPubKeyOrig.SetDestination(newDefaultKey.GetID());
	CScript scriptPubKey;
	scriptPubKey << CScript::EncodeOP_N(OP_OFFER_ACTIVATE) << vchOffer
			<< vchRand << OP_2DROP << OP_DROP;
	scriptPubKey += scriptPubKeyOrig;

	// use the script pub key to create the vecsend which sendmoney takes and puts it into vout
    vector< pair<CScript, int64> > vecSend;
    vecSend.push_back(make_pair(scriptPubKey, MIN_AMOUNT));
	
	CScript scriptFee;
	scriptFee << OP_RETURN;
	vecSend.push_back(make_pair(scriptFee, nNetFee));

	// send the tranasction
	string strError = pwalletMain->SendMoney(vecSend, MIN_AMOUNT, wtx,
				false, bdata);
	if (strError != "")
	{
		throw JSONRPCError(RPC_WALLET_ERROR, strError);
	}
	if(fDebug)
		printf("SENT:OFFERACTIVATE: title=%s, guid=%s, tx=%s\n",
			stringFromVch(newOffer.sTitle).c_str(),
			stringFromVch(vchOffer).c_str(), wtx.GetHash().GetHex().c_str());

	vector<Value> res;
	res.push_back(wtx.GetHash().GetHex());
	res.push_back(HexStr(vchRand));
	return res;
}

Value offerlink(const Array& params, bool fHelp) {
	if (fHelp || params.size() < 2 || params.size() > 3)
		throw runtime_error(
		"offerlink <guid> <commission> [description]\n"
						"<guid> offer guid that you are linking to\n"
						"<commission> percentage of profit desired over original offer price, > 0, ie: 5 for 5%\n"
						"<description> description, 64 KB max. Defaults to original description. Leave as '' to use default.\n"
						+ HelpRequiringPassphrase());
	if(!HasReachedMainNetForkB2())
		throw runtime_error("Please wait until B2 hardfork starts in before executing this command.");
	// gather inputs
	string baSig;
	unsigned int nQty;
	CWalletTx wtxCertIn;
	COfferLinkWhitelistEntry whiteListEntry;

	vector<unsigned char> vchLinkOffer = vchFromValue(params[0]);
	vector<unsigned char> vchTitle;
	vector<unsigned char> vchDesc;
	vector<unsigned char> vchCat;
	// look for a transaction with this key
	CTransaction tx;
	COffer linkOffer;
	if (!GetTxOfOffer(*pofferdb, vchLinkOffer, linkOffer, tx) || vchLinkOffer.empty())
		throw runtime_error("could not find an offer with this name");

	if(!linkOffer.vchLinkOffer.empty())
	{
		throw runtime_error("cannot link to an offer that is already linked to another offer");
	}

	int commissionInteger = atoi(params[1].get_str().c_str());
	if(commissionInteger < 0 || commissionInteger > 255)
	{
		throw JSONRPCError(RPC_INVALID_PARAMETER, "commission must positive and less than 256!");
	}
	
	if(params.size() >= 3)
	{

		vchDesc = vchFromValue(params[2]);
		if(vchDesc.size() > 0)
		{
			// 64Kbyte offer desc. maxlen
			if (vchDesc.size() > 1024 * 64)
				throw JSONRPCError(RPC_INVALID_PARAMETER, "offer description cannot exceed 65536 bytes!");
		}
		else
		{
			vchDesc = linkOffer.sDescription;
		}
	}
	else
	{
		vchDesc = linkOffer.sDescription;
	}	


	
	COfferLinkWhitelistEntry foundEntry;

	// go through the whitelist and see if you own any of the certs to apply to this offer for a discount
	for(unsigned int i=0;i<linkOffer.linkWhitelist.entries.size();i++) {
		CTransaction txCert;
		CWalletTx wtxCertIn;
		CCert theCert;
		COfferLinkWhitelistEntry& entry = linkOffer.linkWhitelist.entries[i];
		// make sure this cert is still valid
		if (GetTxOfCert(*pcertdb, entry.certLinkVchRand, theCert, txCert))
		{
			// make sure its in your wallet (you control this cert)		
			if (IsCertMine(txCert) && pwalletMain->GetTransaction(txCert.GetHash(), wtxCertIn)) 
			{
				foundEntry = entry;
				break;	
			}
			
		}

	}


	// if the whitelist exclusive mode is on and you dont have a cert in the whitelist, you cannot link to this offer
	if(foundEntry.IsNull() && linkOffer.linkWhitelist.bExclusiveResell)
	{
		throw runtime_error("cannot link to this offer because you don't own a cert from its whitelist (the offer is in exclusive mode)");
	}

	vchCat = linkOffer.sCategory;
	vchTitle = linkOffer.sTitle;
	nQty = linkOffer.nQty;

	// this is a syscoin transaction
	CWalletTx wtx;
	wtx.nVersion = SYSCOIN_TX_VERSION;


	// generate rand identifier
	uint64 rand = GetRand((uint64) -1);
	vector<unsigned char> vchRand = CBigNum(rand).getvch();
	vector<unsigned char> vchOffer = vchFromString(HexStr(vchRand));
	int precision = 2;
	// get precision
	convertCurrencyCodeToSyscoin(linkOffer.sCurrencyCode, linkOffer.GetPrice(), nBestHeight, precision);
	float minPrice = 1/pow(10,precision);
	float price = linkOffer.GetPrice();
	if(price < minPrice)
		price = minPrice;
	string priceStr = strprintf("%.*f", precision, price);
	price = (float)atof(priceStr.c_str());

	EnsureWalletIsUnlocked();
	// calculate network fees
	int64 nNetFee = GetOfferNetworkFee(OP_OFFER_ACTIVATE, nBestHeight);	
	// unserialize offer object from txn, serialize back
	// build offer object
	COffer newOffer;
	newOffer.vchPubKey = linkOffer.vchPubKey;
	newOffer.vchRand = vchOffer;
	newOffer.sCategory = vchCat;
	newOffer.sTitle = vchTitle;
	newOffer.sDescription = vchDesc;
	newOffer.nQty = nQty;
	newOffer.linkWhitelist.bExclusiveResell = true;
	newOffer.SetPrice(price);
	
	newOffer.nCommission = commissionInteger;
	
	newOffer.vchLinkOffer = vchLinkOffer;
	newOffer.nHeight = nBestHeight;
	newOffer.sCurrencyCode = linkOffer.sCurrencyCode;
	string bdata = newOffer.SerializeToString();
	
	//create offeractivate txn keys
	CPubKey newDefaultKey;
	pwalletMain->GetKeyFromPool(newDefaultKey, false);
	CScript scriptPubKeyOrig;
	scriptPubKeyOrig.SetDestination(newDefaultKey.GetID());
	CScript scriptPubKey;
	scriptPubKey << CScript::EncodeOP_N(OP_OFFER_ACTIVATE) << vchOffer
			<< vchRand << OP_2DROP << OP_DROP;
	scriptPubKey += scriptPubKeyOrig;


	string strError;

	// send the tranasction
	// use the script pub key to create the vecsend which sendmoney takes and puts it into vout
	vector< pair<CScript, int64> > vecSend;
	vecSend.push_back(make_pair(scriptPubKey, MIN_AMOUNT));
	
	CScript scriptFee;
	scriptFee << OP_RETURN;
	vecSend.push_back(make_pair(scriptFee, nNetFee));
	strError = pwalletMain->SendMoney(vecSend, MIN_AMOUNT, wtx,
			false, bdata);

	if (strError != "")
	{
		throw JSONRPCError(RPC_WALLET_ERROR, strError);
	}
	if(fDebug)
		printf("SENT:OFFERACTIVATE: title=%s, guid=%s, tx=%s\n",
			stringFromVch(newOffer.sTitle).c_str(),
			stringFromVch(vchOffer).c_str(), wtx.GetHash().GetHex().c_str());

	vector<Value> res;
	res.push_back(wtx.GetHash().GetHex());
	res.push_back(HexStr(vchRand));
	return res;
}
Value offeraddwhitelist(const Array& params, bool fHelp) {
	if (fHelp || params.size() < 2 || params.size() > 3)
		throw runtime_error(
		"offeraddwhitelist <offer guid> <cert guid> [discount percentage]\n"
		"Add to the whitelist of your offer(controls who can resell).\n"
						"<offer guid> offer guid that you are adding to\n"
						"<cert guid> cert guid representing a certificate that you control (transfer it to reseller after)\n"
						"<discount percentage> percentage of discount given to reseller for this offer. Negative discount adds on top of offer price, acts as an extra commission. -99 to 99.\n"						
						+ HelpRequiringPassphrase());

	if(!HasReachedMainNetForkB2())
		throw runtime_error("Please wait until B2 hardfork starts in before executing this command.");	
	// gather & validate inputs
	vector<unsigned char> vchOffer = vchFromValue(params[0]);
	vector<unsigned char> vchCert =  vchFromValue(params[1]);
	int nDiscountPctInteger = 0;
	
	if(params.size() >= 3)
		nDiscountPctInteger = atoi(params[2].get_str().c_str());

	if(nDiscountPctInteger < -99 || nDiscountPctInteger > 99)
		throw runtime_error("Invalid discount amount");
	CTransaction txCert;
	CCert theCert;
	CWalletTx wtx, wtxIn, wtxCertIn;
	if (!GetTxOfCert(*pcertdb, vchCert, theCert, txCert))
		throw runtime_error("could not find a certificate with this key");

	// check to see if certificate in wallet
	if (!pwalletMain->GetTransaction(txCert.GetHash(), wtxCertIn)) 
		throw runtime_error("this certificate is not in your wallet");

	// this is a syscoind txn
	wtx.nVersion = SYSCOIN_TX_VERSION;
	CScript scriptPubKeyOrig;

	// get a key from our wallet set dest as ourselves
	CPubKey newDefaultKey;
	pwalletMain->GetKeyFromPool(newDefaultKey, false);
	scriptPubKeyOrig.SetDestination(newDefaultKey.GetID());


	// check for existing pending offers
	if (ExistsInMempool(vchOffer, OP_OFFER_REFUND) || ExistsInMempool(vchOffer, OP_OFFER_ACTIVATE) || ExistsInMempool(vchOffer, OP_OFFER_UPDATE)) {
		throw runtime_error("there are pending operations or refunds on that offer");
	}
	EnsureWalletIsUnlocked();

	// look for a transaction with this key
	CTransaction tx;
	COffer theOffer;
	if (!GetTxOfOffer(*pofferdb, vchOffer, theOffer, tx))
		throw runtime_error("could not find an offer with this name");
	if (!pwalletMain->GetTransaction(tx.GetHash(), wtxIn)) 
		throw runtime_error("this offer is not in your wallet");
	// unserialize offer object from txn
	if(!theOffer.UnserializeFromTx(tx))
		throw runtime_error("cannot unserialize offer from txn");

	// get the offer from DB
	vector<COffer> vtxPos;
	if (!pofferdb->ReadOffer(vchOffer, vtxPos))
		throw runtime_error("could not read offer from DB");

	theOffer = vtxPos.back();
	theOffer.nHeight = nBestHeight;
	for(unsigned int i=0;i<theOffer.linkWhitelist.entries.size();i++) {
		COfferLinkWhitelistEntry& entry = theOffer.linkWhitelist.entries[i];
		// make sure this cert doesn't already exist
		if (entry.certLinkVchRand == vchCert)
		{
			throw runtime_error("This cert is already added to your whitelist");
		}

	}
	// create OFFERUPDATE txn keys
	CScript scriptPubKey;
	scriptPubKey << CScript::EncodeOP_N(OP_OFFER_UPDATE) << vchOffer << theOffer.sTitle
			<< OP_2DROP << OP_DROP;
	scriptPubKey += scriptPubKeyOrig;
	// calculate network fees
	int64 nNetFee = GetOfferNetworkFee(OP_OFFER_UPDATE, nBestHeight);
	

	theOffer.accepts.clear();

	COfferLinkWhitelistEntry entry;
	entry.certLinkVchRand = theCert.vchRand;
	entry.nDiscountPct = nDiscountPctInteger;
	theOffer.linkWhitelist.PutWhitelistEntry(entry);
	// serialize offer object
	string bdata = theOffer.SerializeToString();

	string strError = SendOfferMoneyWithInputTx(scriptPubKey, MIN_AMOUNT, nNetFee,
			wtxIn, wtx, false, bdata);
	if (strError != "")
		throw JSONRPCError(RPC_WALLET_ERROR, strError);

	return wtx.GetHash().GetHex();
}
Value offerremovewhitelist(const Array& params, bool fHelp) {
	if (fHelp || params.size() != 2)
		throw runtime_error(
		"offerremovewhitelist <offer guid> <cert guid>\n"
		"Remove from the whitelist of your offer(controls who can resell).\n"
						+ HelpRequiringPassphrase());
	if(!HasReachedMainNetForkB2())
		throw runtime_error("Please wait until B2 hardfork starts in before executing this command.");	
	// gather & validate inputs
	vector<unsigned char> vchOffer = vchFromValue(params[0]);
	vector<unsigned char> vchCert = vchFromValue(params[1]);


	CCert theCert;

	// this is a syscoind txn
	CWalletTx wtx, wtxIn;
	wtx.nVersion = SYSCOIN_TX_VERSION;
	CScript scriptPubKeyOrig;

	// get a key from our wallet set dest as ourselves
	CPubKey newDefaultKey;
	pwalletMain->GetKeyFromPool(newDefaultKey, false);
	scriptPubKeyOrig.SetDestination(newDefaultKey.GetID());

	
	// check for existing pending offers
	if (ExistsInMempool(vchOffer, OP_OFFER_REFUND) || ExistsInMempool(vchOffer, OP_OFFER_ACTIVATE) || ExistsInMempool(vchOffer, OP_OFFER_UPDATE)) {
		throw runtime_error("there are pending operations or refunds on that offer");
	}
	EnsureWalletIsUnlocked();

	// look for a transaction with this key
	CTransaction tx;
	COffer theOffer;
	if (!GetTxOfOffer(*pofferdb, vchOffer, theOffer, tx))
		throw runtime_error("could not find an offer with this name");
	if (!pwalletMain->GetTransaction(tx.GetHash(), wtxIn)) 
		throw runtime_error("this offer is not in your wallet");
	// unserialize offer object from txn
	if(!theOffer.UnserializeFromTx(tx))
		throw runtime_error("cannot unserialize offer from txn");

	// get the offer from DB
	vector<COffer> vtxPos;
	if (!pofferdb->ReadOffer(vchOffer, vtxPos))
		throw runtime_error("could not read offer from DB");

	theOffer = vtxPos.back();
	theOffer.nHeight = nBestHeight;
	// create OFFERUPDATE txn keys
	CScript scriptPubKey;
	scriptPubKey << CScript::EncodeOP_N(OP_OFFER_UPDATE) << vchOffer << theOffer.sTitle
			<< OP_2DROP << OP_DROP;
	scriptPubKey += scriptPubKeyOrig;
	// calculate network fees
	int64 nNetFee = GetOfferNetworkFee(OP_OFFER_UPDATE, nBestHeight);
	theOffer.accepts.clear();


	if(!theOffer.linkWhitelist.RemoveWhitelistEntry(vchCert))
	{
		throw runtime_error("could not find remove this whitelist entry");
	}

	// serialize offer object
	string bdata = theOffer.SerializeToString();

	string strError = SendOfferMoneyWithInputTx(scriptPubKey, MIN_AMOUNT, nNetFee,
			wtxIn, wtx, false, bdata);
	if (strError != "")
		throw JSONRPCError(RPC_WALLET_ERROR, strError);

	return wtx.GetHash().GetHex();
}
Value offerclearwhitelist(const Array& params, bool fHelp) {
	if (fHelp || params.size() != 1)
		throw runtime_error(
		"offerclearwhitelist <offer guid>\n"
		"Clear the whitelist of your offer(controls who can resell).\n"
						+ HelpRequiringPassphrase());
	if(!HasReachedMainNetForkB2())
		throw runtime_error("Please wait until B2 hardfork starts in before executing this command.");	
	// gather & validate inputs
	vector<unsigned char> vchOffer = vchFromValue(params[0]);


	CCert theCert;

	// this is a syscoind txn
	CWalletTx wtx, wtxIn;
	wtx.nVersion = SYSCOIN_TX_VERSION;
	CScript scriptPubKeyOrig;

	// get a key from our wallet set dest as ourselves
	CPubKey newDefaultKey;
	pwalletMain->GetKeyFromPool(newDefaultKey, false);
	scriptPubKeyOrig.SetDestination(newDefaultKey.GetID());


	// check for existing pending offers
	if (ExistsInMempool(vchOffer, OP_OFFER_REFUND) || ExistsInMempool(vchOffer, OP_OFFER_ACTIVATE) || ExistsInMempool(vchOffer, OP_OFFER_UPDATE)) {
		throw runtime_error("there are pending operations or refunds on that offer");
	}
	EnsureWalletIsUnlocked();

	// look for a transaction with this key
	CTransaction tx;
	COffer theOffer;
	if (!GetTxOfOffer(*pofferdb, vchOffer, theOffer, tx))
		throw runtime_error("could not find an offer with this name");
	if (!pwalletMain->GetTransaction(tx.GetHash(), wtxIn)) 
		throw runtime_error("this offer is not in your wallet");
	// unserialize offer object from txn
	if(!theOffer.UnserializeFromTx(tx))
		throw runtime_error("cannot unserialize offer from txn");

	// get the offer from DB
	vector<COffer> vtxPos;
	if (!pofferdb->ReadOffer(vchOffer, vtxPos))
		throw runtime_error("could not read offer from DB");

	theOffer = vtxPos.back();
	theOffer.nHeight = nBestHeight;
	// create OFFERUPDATE txn keys
	CScript scriptPubKey;
	scriptPubKey << CScript::EncodeOP_N(OP_OFFER_UPDATE) << vchOffer << theOffer.sTitle
			<< OP_2DROP << OP_DROP;
	scriptPubKey += scriptPubKeyOrig;
	// calculate network fees
	int64 nNetFee = GetOfferNetworkFee(OP_OFFER_UPDATE, nBestHeight);
	
	theOffer.accepts.clear();
	theOffer.linkWhitelist.entries.clear();

	// serialize offer object
	string bdata = theOffer.SerializeToString();

	string strError = SendOfferMoneyWithInputTx(scriptPubKey, MIN_AMOUNT, nNetFee,
			wtxIn, wtx, false, bdata);
	if (strError != "")
		throw JSONRPCError(RPC_WALLET_ERROR, strError);

	return wtx.GetHash().GetHex();
}

Value offerwhitelist(const Array& params, bool fHelp) {
    if (fHelp || 1 != params.size())
        throw runtime_error("offerwhitelist <offer guid>\n"
                "List all whitelist entries for this offer.\n");
	if(!HasReachedMainNetForkB2())
		throw runtime_error("Please wait until B2 hardfork starts in before executing this command.");	
    Array oRes;
    vector<unsigned char> vchOffer = vchFromValue(params[0]);
	// look for a transaction with this key
	CTransaction tx;
	COffer theOffer;
	if (!GetTxOfOffer(*pofferdb, vchOffer, theOffer, tx))
		throw runtime_error("could not find an offer with this name");
	
	for(unsigned int i=0;i<theOffer.linkWhitelist.entries.size();i++) {
		CTransaction txCert;
		CCert theCert;
		COfferLinkWhitelistEntry& entry = theOffer.linkWhitelist.entries[i];
		uint256 hashBlock;
		if (GetTxOfCert(*pcertdb, entry.certLinkVchRand, theCert, txCert))
		{
			Object oList;
			oList.push_back(Pair("cert_guid", stringFromVch(theCert.vchRand)));
			oList.push_back(Pair("cert_title", stringFromVch(theCert.vchTitle)));
			oList.push_back(Pair("cert_is_mine", IsCertMine(txCert) ? "true" : "false"));
			string strAddress = "";
			GetCertAddress(txCert, strAddress);
			oList.push_back(Pair("cert_address", strAddress));
			int expires_in = 0;
			int64 nHeight = GetTxHashHeight(txCert.GetHash());
            if(nHeight + GetCertDisplayExpirationDepth() - pindexBest->nHeight > 0)
			{
				expires_in = nHeight + GetCertDisplayExpirationDepth() - pindexBest->nHeight;
			}  
			oList.push_back(Pair("cert_expiresin",expires_in));
			oList.push_back(Pair("offer_discount_percentage", strprintf("%d%%", entry.nDiscountPct)));
			oRes.push_back(oList);
		}  
    }
    return oRes;
}
Value offerupdate(const Array& params, bool fHelp) {
	if (fHelp || params.size() < 5 || params.size() > 7)
		throw runtime_error(
		"offerupdate <guid> <category> <title> <quantity> <price> [description] [exclusive resell=1]\n"
						"Perform an update on an offer you control.\n"
						+ HelpRequiringPassphrase());

	if(!HasReachedMainNetForkB2())
		throw runtime_error("Please wait until B2 hardfork starts in before executing this command.");	
	// gather & validate inputs
	vector<unsigned char> vchOffer = vchFromValue(params[0]);
	vector<unsigned char> vchCat = vchFromValue(params[1]);
	vector<unsigned char> vchTitle = vchFromValue(params[2]);
	vector<unsigned char> vchDesc;
	bool bExclusiveResell = true;
	unsigned int nQty;
	float price;
	if (params.size() >= 6) vchDesc = vchFromValue(params[5]);
	if(params.size() == 7) bExclusiveResell = atoi(params[6].get_str().c_str()) == 1? true: false;
	try {
		nQty = (unsigned int)atoi(params[3].get_str().c_str());
		price = atof(params[4].get_str().c_str());

	} catch (std::exception &e) {
		throw runtime_error("invalid price and/or quantity values.");
	}
	if(price <= 0)
	{
		throw JSONRPCError(RPC_INVALID_PARAMETER, "offer price must be greater than 0!");
	}
	if(vchCat.size() < 1)
        throw runtime_error("offer category cannot by empty!");
	if(vchTitle.size() < 1)
        throw runtime_error("offer title cannot be empty!");
	if(vchCat.size() > 255)
        throw runtime_error("offer category cannot exceed 255 bytes!");
	if(vchTitle.size() > 255)
        throw runtime_error("offer title cannot exceed 255 bytes!");
    // 64Kbyte offer desc. maxlen
	if (vchDesc.size() > 1024 * 64)
		throw JSONRPCError(RPC_INVALID_PARAMETER, "offer description cannot exceed 65536 bytes!");

	// this is a syscoind txn
	CWalletTx wtx, wtxIn;
	wtx.nVersion = SYSCOIN_TX_VERSION;
	CScript scriptPubKeyOrig;

	// get a key from our wallet set dest as ourselves
	CPubKey newDefaultKey;
	pwalletMain->GetKeyFromPool(newDefaultKey, false);
	scriptPubKeyOrig.SetDestination(newDefaultKey.GetID());

	// create OFFERUPDATE txn keys
	CScript scriptPubKey;
	scriptPubKey << CScript::EncodeOP_N(OP_OFFER_UPDATE) << vchOffer << vchTitle
			<< OP_2DROP << OP_DROP;
	scriptPubKey += scriptPubKeyOrig;

	// check for existing pending offers
	if (ExistsInMempool(vchOffer, OP_OFFER_REFUND) || ExistsInMempool(vchOffer, OP_OFFER_ACTIVATE) || ExistsInMempool(vchOffer, OP_OFFER_UPDATE)) {
		throw runtime_error("there are pending operations or refunds on that offer");
	}
	EnsureWalletIsUnlocked();

	// look for a transaction with this key
	CTransaction tx, linktx;
	COffer theOffer, linkOffer;
	if (!GetTxOfOffer(*pofferdb, vchOffer, theOffer, tx))
		throw runtime_error("could not find an offer with this name");
	if (!pwalletMain->GetTransaction(tx.GetHash(), wtxIn)) 
		throw runtime_error("this offer is not in your wallet");
	// unserialize offer object from txn
	if(!theOffer.UnserializeFromTx(tx))
		throw runtime_error("cannot unserialize offer from txn");

	// get the offer from DB
	vector<COffer> vtxPos;
	if (!pofferdb->ReadOffer(vchOffer, vtxPos))
		throw runtime_error("could not read offer from DB");

	theOffer = vtxPos.back();
		
	// calculate network fees
	int64 nNetFee = GetOfferNetworkFee(OP_OFFER_UPDATE, nBestHeight);
	int precision = 2;
	// get precision
	convertCurrencyCodeToSyscoin(theOffer.sCurrencyCode, theOffer.GetPrice(), nBestHeight, precision);
	float minPrice = 1/pow(10,precision);
	if(price < minPrice)
		price = minPrice;
	string priceStr = strprintf("%.*f", precision, price);
	price = (float)atof(priceStr.c_str());
	// update offer values
	theOffer.sCategory = vchCat;
	theOffer.sTitle = vchTitle;
	theOffer.sDescription = vchDesc;
	unsigned int memPoolQty = QtyOfPendingAcceptsInMempool(vchOffer);
	if((nQty-memPoolQty) < 0)
		throw runtime_error("not enough remaining quantity to fulfill this offerupdate"); // SS i think needs better msg
	
	theOffer.nQty = nQty;
	theOffer.nHeight = nBestHeight;
	theOffer.SetPrice(price);
	theOffer.accepts.clear();
	if(params.size() == 7)
		theOffer.linkWhitelist.bExclusiveResell = bExclusiveResell;
	// serialize offer object
	string bdata = theOffer.SerializeToString();

	string strError = SendOfferMoneyWithInputTx(scriptPubKey, MIN_AMOUNT, nNetFee,
			wtxIn, wtx, false, bdata);
	if (strError != "")
		throw JSONRPCError(RPC_WALLET_ERROR, strError);

	return wtx.GetHash().GetHex();
}

Value offerrefund(const Array& params, bool fHelp) {
	if (fHelp || 1 != params.size())
		throw runtime_error("offerrefund <acceptguid>\n"
				"Refund an offer accept of an offer you control.\n"
				"<guid> guidkey of offer accept to refund.\n"
				+ HelpRequiringPassphrase());
	if(!HasReachedMainNetForkB2())
		throw runtime_error("Please wait until B2 hardfork starts in before executing this command.");
	vector<unsigned char> vchAcceptRand = ParseHex(params[0].get_str());

	EnsureWalletIsUnlocked();

	// look for a transaction with this key
	COffer activateOffer, theOffer;
	CTransaction tmpTx, txOffer, txAccept;
	COfferAccept theOfferAccept;
	uint256 hashBlock;

	if (!GetTxOfOfferAccept(*pofferdb, vchAcceptRand, theOffer, txOffer))
		throw runtime_error("could not find an offer accept with this identifier");

	CWalletTx wtxIn;
	if (!pwalletMain->GetTransaction(txOffer.GetHash(), wtxIn)) 
	{
		throw runtime_error("can't find this offer in your wallet");
	}

	if (ExistsInMempool(theOffer.vchRand, OP_OFFER_REFUND) || ExistsInMempool(theOffer.vchRand, OP_OFFER_ACTIVATE) || ExistsInMempool(theOffer.vchRand, OP_OFFER_UPDATE)) {
		throw runtime_error("there are pending operations or refunds on that offer");
	}
	// check for existence of offeraccept in txn offer obj
	if(!theOffer.GetAcceptByHash(vchAcceptRand, theOfferAccept))
		throw runtime_error("could not read accept from offer txn");
	// check if this offer is linked to another offer
	if (!theOffer.vchLinkOffer.empty())
		throw runtime_error("You cannot refund an offer that is linked to another offer, only the owner of the original offer can issue a refund.");
	
	string strError = makeOfferRefundTX(txOffer, theOffer, theOfferAccept, OFFER_REFUND_PAYMENT_INPROGRESS);
	if (strError != "")
	{
		throw runtime_error(strError);
	}
	return "Success";
}

Value offeraccept(const Array& params, bool fHelp) {
	if (fHelp || 1 > params.size() || params.size() > 5)
		throw runtime_error("offeraccept <guid> [quantity] [message] [refund address] [linkedguid]\n"
				"Accept&Pay for a confirmed offer.\n"
				"<guid> guidkey from offer.\n"
				"<quantity> quantity to buy. Defaults to 1.\n"
				"<message> payment message to seller, 1KB max.\n"
				"<refund address> In case offer not accepted refund to this address. Leave empty to use a new address from your wallet. \n"
				"<linkedguid> guidkey from offer accept linking to this offer accept. For internal use only, leave blank\n"
				+ HelpRequiringPassphrase());
	if(!HasReachedMainNetForkB2())
		throw runtime_error("Please wait until B2 hardfork starts in before executing this command.");	
	vector<unsigned char> vchRefundAddress;	
	CBitcoinAddress refundAddr;	
	vector<unsigned char> vchOffer = vchFromValue(params[0]);
	vector<unsigned char> vchLinkOfferAccept = vchFromValue(params.size()>= 5? params[4]:"");
	vector<unsigned char> vchMessage = vchFromValue(params.size()>=3?params[2]:" ");
	int64 nQty = 1;
	if (params.size() >= 2) {
		try {
			nQty=atoi64(params[1].get_str().c_str());
		} catch (std::exception &e) {
			throw runtime_error("invalid price and/or quantity values.");
		}
		if(nQty < 0)
		{
			throw runtime_error("invalid quantity value.");
		}

	}
	if(params.size() < 4)
	{
		CPubKey newDefaultKey;
		pwalletMain->GetKeyFromPool(newDefaultKey, false);
		refundAddr = CBitcoinAddress(newDefaultKey.GetID());
		vchRefundAddress = vchFromString(refundAddr.ToString());
	}
	else
	{
		vchRefundAddress = vchFromValue(params[3]);
		refundAddr = CBitcoinAddress(stringFromVch(vchRefundAddress));
	}
	if (!refundAddr.IsValid())
		throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,
				"Invalid syscoin address");


    if (vchMessage.size() < 1)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "offeraccept message data cannot be empty!");
    if (vchMessage.size() > MAX_VALUE_LENGTH)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "offeraccept message data cannot exceed 16384 bytes!");

	// this is a syscoin txn
	CWalletTx wtx;
	wtx.nVersion = SYSCOIN_TX_VERSION;
	CScript scriptPubKeyOrig;

	// generate offer accept identifier and hash
	uint64 rand = GetRand((uint64) -1);
	vector<unsigned char> vchAcceptRand = CBigNum(rand).getvch();
	vector<unsigned char> vchAccept = vchFromString(HexStr(vchAcceptRand));

	// get a key from our wallet set dest as ourselves
	CPubKey newDefaultKey;
	pwalletMain->GetKeyFromPool(newDefaultKey, false);
	scriptPubKeyOrig.SetDestination(newDefaultKey.GetID());

	// create OFFERACCEPT txn keys
	CScript scriptPubKey;
	scriptPubKey << CScript::EncodeOP_N(OP_OFFER_ACCEPT)
			<< vchOffer << vchAcceptRand
			<< OP_2DROP << OP_DROP;
	scriptPubKey += scriptPubKeyOrig;

	if (ExistsInMempool(vchOffer, OP_OFFER_REFUND) || ExistsInMempool(vchOffer, OP_OFFER_ACTIVATE) || ExistsInMempool(vchOffer, OP_OFFER_UPDATE)) {
		throw runtime_error("there are pending operations or refunds on that offer");
	}

	EnsureWalletIsUnlocked();

	// look for a transaction with this key
	CTransaction tx;
	COffer theOffer;
	if (!GetTxOfOffer(*pofferdb, vchOffer, theOffer, tx))
	{
		throw runtime_error("could not find an offer with this identifier");
	}

	COffer linkedOffer = theOffer;
	CTransaction tmpTx;
	// check if parent to linked offer is still valid
	if (!linkedOffer.IsNull() && !linkedOffer.vchLinkOffer.empty())
	{
		if(pofferdb->ExistsOffer(linkedOffer.vchLinkOffer))
		{
			if (!GetTxOfOffer(*pofferdb, linkedOffer.vchLinkOffer,linkedOffer, tmpTx))
				throw runtime_error("Trying to accept a linked offer but could not find parent offer, perhaps it is expired");
		}
	}
	COfferLinkWhitelistEntry foundCert;
	CWalletTx wtxCertIn;
	// go through the whitelist and see if you own any of the certs to apply to this offer for a discount
	for(unsigned int i=0;i<theOffer.linkWhitelist.entries.size();i++) {
		CTransaction txCert;
		
		CCert theCert;
		COfferLinkWhitelistEntry& entry = theOffer.linkWhitelist.entries[i];
		// make sure this cert is still valid
		if (GetTxOfCert(*pcertdb, entry.certLinkVchRand, theCert, txCert))
		{
			// make sure its in your wallet (you control this cert)
			
			if (IsCertMine(txCert) && pwalletMain->GetTransaction(txCert.GetHash(), wtxCertIn)) 
			{
				foundCert = entry;
				break;
			}
			
		}

	}
	// if this is an accept for a linked offer, the offer is set to exclusive mode and you dont have a cert in the whitelist, you cannot accept this offer
	if(vchLinkOfferAccept.size() > 0 && foundCert.IsNull() && theOffer.linkWhitelist.bExclusiveResell)
	{
		throw runtime_error("cannot pay for this linked offer because you don't own a cert from its whitelist");
	}
	unsigned int memPoolQty = QtyOfPendingAcceptsInMempool(vchOffer);
	if(theOffer.nQty < (nQty+memPoolQty))
		throw runtime_error("not enough remaining quantity to fulfill this orderaccept");
	int precision = 2;
	int64 nPrice = convertCurrencyCodeToSyscoin(theOffer.sCurrencyCode, theOffer.GetPrice(foundCert), nBestHeight, precision);
	string strCipherText = "";
	if(vchLinkOfferAccept.size() <= 0)
	{
		if(!EncryptMessage(theOffer.vchPubKey, vchMessage, strCipherText))
			throw runtime_error("could not encrypt message to seller");
	}

	// create accept object
	COfferAccept txAccept;
	txAccept.vchRand = vchAcceptRand;
	if(strCipherText.length() > 0)
		txAccept.vchMessage = vchFromString(strCipherText);
	else
		txAccept.vchMessage = vchMessage;
	txAccept.nQty = nQty;
	txAccept.nPrice = theOffer.GetPrice(foundCert);
	txAccept.vchLinkOfferAccept = vchLinkOfferAccept;
	txAccept.vchRefundAddress = vchRefundAddress;
	txAccept.nHeight = nBestHeight;
	txAccept.bPaid = true;
	txAccept.vchCertLink = foundCert.certLinkVchRand;
	theOffer.accepts.clear();
	theOffer.PutOfferAccept(txAccept);
	
	
	// Check for sufficient funds to pay for order
    set<pair<const CWalletTx*,unsigned int> > setCoins;
    int64 nValueIn = 0;


    int64 nTotalValue = ( nPrice * nQty );

    if (!pwalletMain->SelectCoins(nTotalValue + (MIN_AMOUNT * 2), setCoins, nValueIn))
        throw runtime_error("insufficient funds to pay for offer");



    vector< pair<CScript, int64> > vecSend;
    

    CScript scriptPayment;
	string strAddress = "";
    GetOfferAddress(tx, strAddress);
	CBitcoinAddress address(strAddress);
	if(!address.IsValid())
	{
		throw runtime_error("payment to invalid address");
	}
    scriptPayment.SetDestination(address.Get());

    vecSend.push_back(make_pair(scriptPayment, nTotalValue));
	vecSend.push_back(make_pair(scriptPubKey, MIN_AMOUNT));
	// serialize offer object
	string bdata = theOffer.SerializeToString();
	string strError;
	if(!foundCert.IsNull())
	{
		strError = SendCertMoneyWithInputTx(vecSend, MIN_AMOUNT+nTotalValue, 0, wtxCertIn,
			wtx, false, bdata);
		if (strError != "")
			throw JSONRPCError(RPC_WALLET_ERROR, strError);		
		// create a certupdate passing in wtx (offeraccept) as input to keep chain of inputs going for next cert transaction (since we used the last cert tx as input to sendoffermoneywithinputtx)
		CWalletTx wtxCert;
		wtxCert.nVersion = SYSCOIN_TX_VERSION;
		CScript scriptPubKeyOrig;
		CCert cert;
		CTransaction tx;
		if (!GetTxOfCert(*pcertdb, foundCert.certLinkVchRand, cert, tx))
			throw runtime_error("could not find a certificate with this key");


		// get a key from our wallet set dest as ourselves
		CPubKey newDefaultKey;
		pwalletMain->GetKeyFromPool(newDefaultKey, false);
		scriptPubKeyOrig.SetDestination(newDefaultKey.GetID());

		// create CERTUPDATE txn keys
		CScript scriptPubKey;
		scriptPubKey << CScript::EncodeOP_N(OP_CERT_UPDATE) << cert.vchRand << cert.vchTitle
				<< OP_2DROP << OP_DROP;
		scriptPubKey += scriptPubKeyOrig;

		int64 nNetFee = GetCertNetworkFee(OP_CERT_UPDATE, nBestHeight);
		cert.nHeight = nBestHeight;
		strError = SendOfferMoneyWithInputTx(scriptPubKey, MIN_AMOUNT, nNetFee,
				wtx, wtxCert, false, cert.SerializeToString());
		if (strError != "")
			throw JSONRPCError(RPC_WALLET_ERROR, strError);


	}
	else
	{
		strError = pwalletMain->SendMoney(vecSend, MIN_AMOUNT+nTotalValue, wtx,
			false, bdata);
	}

    if (strError != "")
	{
        throw JSONRPCError(RPC_WALLET_ERROR, strError);
	}
	
	return wtx.GetHash().GetHex();
}


Value offerinfo(const Array& params, bool fHelp) {
	if (fHelp || 1 != params.size())
		throw runtime_error("offerinfo <guid>\n"
				"Show values of an offer.\n");

	Object oLastOffer;
	vector<unsigned char> vchOffer = vchFromValue(params[0]);
	string offer = stringFromVch(vchOffer);
	{
		vector<COffer> vtxPos;
		if (!pofferdb->ReadOffer(vchOffer, vtxPos))
			throw JSONRPCError(RPC_WALLET_ERROR,
					"failed to read from offer DB");
		if (vtxPos.size() < 1)
			throw JSONRPCError(RPC_WALLET_ERROR, "no result returned");

        // get transaction pointed to by offer
        CTransaction tx;
        uint256 blockHash;
        uint256 txHash = vtxPos.back().txHash;
        if (!GetTransaction(txHash, tx, blockHash, true))
            throw JSONRPCError(RPC_WALLET_ERROR, "failed to read transaction from disk");

        COffer theOffer = vtxPos.back();

		Object oOffer;
		vector<unsigned char> vchValue;
		Array aoOfferAccepts;
		for(unsigned int i=0;i<theOffer.accepts.size();i++) {
			COfferAccept ca = theOffer.accepts[i];
			Object oOfferAccept;

	        // get transaction pointed to by offer

	        CTransaction txA;
	        uint256 blockHashA;
	        uint256 txHashA= ca.txHash;
	        if (!GetTransaction(txHashA, txA, blockHashA, true))
	            throw JSONRPCError(RPC_WALLET_ERROR, "failed to read transaction from disk");

			string sTime = strprintf("%llu", ca.nTime);
            string sHeight = strprintf("%llu", ca.nHeight);
			oOfferAccept.push_back(Pair("id", HexStr(ca.vchRand)));
			oOfferAccept.push_back(Pair("txid", ca.txHash.GetHex()));
			oOfferAccept.push_back(Pair("height", sHeight));
			oOfferAccept.push_back(Pair("time", sTime));
			oOfferAccept.push_back(Pair("quantity", strprintf("%u", ca.nQty)));
			oOfferAccept.push_back(Pair("currency", stringFromVch(theOffer.sCurrencyCode)));
			int precision = 2;
			convertCurrencyCodeToSyscoin(theOffer.sCurrencyCode, 0, nBestHeight, precision);
			convertCurrencyCodeToSyscoin(theOffer.sCurrencyCode, 0, nBestHeight, precision);
			oOfferAccept.push_back(Pair("price", strprintf("%.*f", precision, ca.nPrice ))); 
			oOfferAccept.push_back(Pair("total", strprintf("%.*f", precision, ca.nPrice * ca.nQty )));
			oOfferAccept.push_back(Pair("total", strprintf("%.*f", precision, ca.nPrice * ca.nQty )));
			COfferLinkWhitelistEntry entry;
			if(IsOfferMine(tx)) 
				theOffer.linkWhitelist.GetLinkEntryByHash(ca.vchCertLink, entry);
			oOfferAccept.push_back(Pair("offer_discount_percentage", strprintf("%d%%", entry.nDiscountPct)));
			oOfferAccept.push_back(Pair("is_mine", IsOfferMine(txA) ? "true" : "false"));
			if(ca.bPaid) {
				oOfferAccept.push_back(Pair("paid","true"));
				string strMessage = string("");
				if(!DecryptMessage(theOffer.vchPubKey, ca.vchMessage, strMessage))
					strMessage = string("Encrypted for owner of offer");
				oOfferAccept.push_back(Pair("pay_message", strMessage));

			}
			else
			{
				oOfferAccept.push_back(Pair("paid","false"));
				oOfferAccept.push_back(Pair("pay_message",""));
			}
			if(ca.bRefunded) { 
				oOfferAccept.push_back(Pair("refunded", "true"));
				oOfferAccept.push_back(Pair("refund_txid", ca.txRefundId.GetHex()));
			}
			else
			{
				oOfferAccept.push_back(Pair("refunded", "false"));
				oOfferAccept.push_back(Pair("refund_txid", ""));
			}
			
			

			aoOfferAccepts.push_back(oOfferAccept);
		}
		int nHeight;
		uint256 offerHash;
		int expired;
		int expires_in;
		int expired_block;
  
		expired = 0;	
		expires_in = 0;
		expired_block = 0;
        if (GetValueOfOfferTxHash(txHash, vchValue, offerHash, nHeight)) {
			oOffer.push_back(Pair("offer", offer));
			oOffer.push_back(Pair("txid", tx.GetHash().GetHex()));
			expired_block = nHeight + GetOfferDisplayExpirationDepth();
            if(nHeight + GetOfferDisplayExpirationDepth() - pindexBest->nHeight <= 0)
			{
				expired = 1;
			}  
			if(expired == 0)
			{
				expires_in = nHeight + GetOfferDisplayExpirationDepth() - pindexBest->nHeight;
			}
			oOffer.push_back(Pair("expires_in", expires_in));
			oOffer.push_back(Pair("expired_block", expired_block));
			oOffer.push_back(Pair("expired", expired));

			string strAddress = "";
            GetOfferAddress(tx, strAddress);
            oOffer.push_back(Pair("address", strAddress));
			oOffer.push_back(Pair("category", stringFromVch(theOffer.sCategory)));
			oOffer.push_back(Pair("title", stringFromVch(theOffer.sTitle)));
			oOffer.push_back(Pair("quantity", strprintf("%u", theOffer.nQty)));
			oOffer.push_back(Pair("currency", stringFromVch(theOffer.sCurrencyCode)));
			
			
			int precision = 2;
			convertCurrencyCodeToSyscoin(theOffer.sCurrencyCode, 0, nBestHeight, precision);
			oOffer.push_back(Pair("price", strprintf("%.*f", precision, theOffer.GetPrice() ))); 
			
			oOffer.push_back(Pair("is_mine", IsOfferMine(tx) ? "true" : "false"));
			if(!theOffer.vchLinkOffer.empty() && IsOfferMine(tx)) {
				oOffer.push_back(Pair("commission", strprintf("%d%%", theOffer.nCommission)));
				oOffer.push_back(Pair("offerlink", "true"));
				oOffer.push_back(Pair("offerlink_guid", stringFromVch(theOffer.vchLinkOffer)));
			}
			else
			{
				oOffer.push_back(Pair("commission", "0"));
				oOffer.push_back(Pair("offerlink", "false"));
			}
			oOffer.push_back(Pair("exclusive_resell", theOffer.linkWhitelist.bExclusiveResell ? "ON" : "OFF"));
			oOffer.push_back(Pair("description", stringFromVch(theOffer.sDescription)));
			oOffer.push_back(Pair("accepts", aoOfferAccepts));
			oLastOffer = oOffer;
		}
	}
	return oLastOffer;

}
Value offeracceptlist(const Array& params, bool fHelp) {
    if (fHelp || 1 < params.size())
		throw runtime_error("offeracceptlist [offer]\n"
				"list my offer accepts");

    vector<unsigned char> vchName;

    if (params.size() == 1)
        vchName = vchFromValue(params[0]);

    Array oRes;
    {

        uint256 blockHash;
        uint256 hash;
        CTransaction tx;
        int nHeight;
		
        BOOST_FOREACH(PAIRTYPE(const uint256, CWalletTx)& item, pwalletMain->mapWallet)
        {
			Object oOfferAccept;
            // get txn hash, read txn index
            hash = item.second.GetHash();

            if (!GetTransaction(hash, tx, blockHash, true))
                continue;

            // skip non-syscoin txns
            if (tx.nVersion != SYSCOIN_TX_VERSION)
                continue;

            // decode txn, skip non-alias txns
            vector<vector<unsigned char> > vvch;
            int op, nOut;
            if (!DecodeOfferTx(tx, op, nOut, vvch, -1) 
            	|| !IsOfferOp(op) 
            	|| (op != OP_OFFER_ACCEPT))
                continue;

            // get the txn height
            nHeight = GetTxHashHeight(hash);

            // get the txn alias name
            if(!GetNameOfOfferTx(tx, vchName))
                continue;
			
			vector<COffer> vtxPos;
			COfferAccept theOfferAccept;

			if (!pofferdb->ReadOffer(vchName, vtxPos))
				continue;
			COffer theOffer = vtxPos.back();
			// Check hash
			const vector<unsigned char> &vchAcceptRand = vvch[1];
			
			// check for existence of offeraccept in txn offer obj
			if(!theOffer.GetAcceptByHash(vchAcceptRand, theOfferAccept))
				continue;
			string offer = stringFromVch(vchName);
			string sHeight = strprintf("%u", theOfferAccept.nHeight);
			oOfferAccept.push_back(Pair("offer", offer));
			oOfferAccept.push_back(Pair("title", stringFromVch(theOffer.sTitle)));
			oOfferAccept.push_back(Pair("id", HexStr(theOfferAccept.vchRand)));
			oOfferAccept.push_back(Pair("height", sHeight));
			oOfferAccept.push_back(Pair("quantity", strprintf("%u", theOfferAccept.nQty)));
			oOfferAccept.push_back(Pair("currency", stringFromVch(theOffer.sCurrencyCode)));
			int precision = 2;
			convertCurrencyCodeToSyscoin(theOffer.sCurrencyCode, 0, nBestHeight, precision);
			convertCurrencyCodeToSyscoin(theOffer.sCurrencyCode, 0, nBestHeight, precision);
			oOfferAccept.push_back(Pair("price", strprintf("%.*f", precision, theOfferAccept.nPrice ))); 
			oOfferAccept.push_back(Pair("total", strprintf("%.*f", precision, theOfferAccept.nPrice * theOfferAccept.nQty ))); 
			
			oOfferAccept.push_back(Pair("is_mine", IsOfferMine(tx) ? "true" : "false"));
			if(theOfferAccept.bPaid && !theOfferAccept.bRefunded) {
				oOfferAccept.push_back(Pair("status","paid"));
			}
			else if(!theOfferAccept.bRefunded)
			{
				oOfferAccept.push_back(Pair("status","not paid"));
			}
			else if(theOfferAccept.bRefunded) { 
				oOfferAccept.push_back(Pair("status", "refunded"));
			}

			oRes.push_back(oOfferAccept);	
        }
    }

    return oRes;
}
Value offerlist(const Array& params, bool fHelp) {
    if (fHelp || 1 < params.size())
		throw runtime_error("offerlist [offer]\n"
				"list my own offers");

    vector<unsigned char> vchName;

    if (params.size() == 1)
        vchName = vchFromValue(params[0]);

    vector<unsigned char> vchNameUniq;
    if (params.size() == 1)
        vchNameUniq = vchFromValue(params[0]);

    Array oRes;
    map< vector<unsigned char>, int > vNamesI;
    map< vector<unsigned char>, Object > vNamesO;

    {

        uint256 blockHash;
        uint256 hash;
        CTransaction tx;
		int expired;
		int pending;
		int expires_in;
		int expired_block;
        int nHeight;

        BOOST_FOREACH(PAIRTYPE(const uint256, CWalletTx)& item, pwalletMain->mapWallet)
        {
			expired = 0;
			pending = 0;
			expires_in = 0;
			expired_block = 0;
            // get txn hash, read txn index
            hash = item.second.GetHash();

            if (!GetTransaction(hash, tx, blockHash, true))
                continue;

            // skip non-syscoin txns
            if (tx.nVersion != SYSCOIN_TX_VERSION)
                continue;

            // decode txn, skip non-alias txns
            vector<vector<unsigned char> > vvch;
            int op, nOut;
            if (!DecodeOfferTx(tx, op, nOut, vvch, -1) 
            	|| !IsOfferOp(op) 
            	|| (op == OP_OFFER_ACCEPT))
                continue;

            // get the txn height
            nHeight = GetTxHashHeight(hash);

            // get the txn alias name
            if(!GetNameOfOfferTx(tx, vchName))
                continue;

			// skip this offer if it doesn't match the given filter value
			if (vchNameUniq.size() > 0 && vchNameUniq != vchName)
				continue;
			// get last active name only
			if (vNamesI.find(vchName) != vNamesI.end() && (nHeight < vNamesI[vchName] || vNamesI[vchName] < 0))
				continue;
			
			vector<COffer> vtxPos;
			COffer theOfferA;
			if (!pofferdb->ReadOffer(vchName, vtxPos))
			{
				pending = 1;
				theOfferA = COffer(tx);
			}
			if (vtxPos.size() < 1)
			{
				pending = 1;
				theOfferA = COffer(tx);
			}	
			if(pending != 1)
			{
				theOfferA = vtxPos.back();
			}
			
            // build the output object
            Object oName;
            oName.push_back(Pair("offer", stringFromVch(vchName)));
            oName.push_back(Pair("title", stringFromVch(theOfferA.sTitle)));
            oName.push_back(Pair("category", stringFromVch(theOfferA.sCategory)));
            oName.push_back(Pair("description", stringFromVch(theOfferA.sDescription)));
			int precision = 2;
			convertCurrencyCodeToSyscoin(theOfferA.sCurrencyCode, 0, nBestHeight, precision);
			oName.push_back(Pair("price", strprintf("%.*f", precision, theOfferA.GetPrice() ))); 	

			oName.push_back(Pair("currency", stringFromVch(theOfferA.sCurrencyCode) ) );
			oName.push_back(Pair("commission", strprintf("%d%%", theOfferA.nCommission)));
            oName.push_back(Pair("quantity", strprintf("%u", theOfferA.nQty)));
			string strAddress = "";
            GetOfferAddress(tx, strAddress);
            oName.push_back(Pair("address", strAddress));
			oName.push_back(Pair("exclusive_resell", theOfferA.linkWhitelist.bExclusiveResell ? "ON" : "OFF"));
			expired_block = nHeight + GetOfferDisplayExpirationDepth();
            if(pending == 0 && (nHeight + GetOfferDisplayExpirationDepth() - pindexBest->nHeight <= 0))
			{
				expired = 1;
			}  
			if(pending == 0 && expired == 0)
			{
				expires_in = nHeight + GetOfferDisplayExpirationDepth() - pindexBest->nHeight;
			}
			oName.push_back(Pair("expires_in", expires_in));
			oName.push_back(Pair("expires_on", expired_block));
			oName.push_back(Pair("expired", expired));
			oName.push_back(Pair("pending", pending));

            vNamesI[vchName] = nHeight;
            vNamesO[vchName] = oName;
        }
    }

    BOOST_FOREACH(const PAIRTYPE(vector<unsigned char>, Object)& item, vNamesO)
        oRes.push_back(item.second);

    return oRes;
}


Value offerhistory(const Array& params, bool fHelp) {
	if (fHelp || 1 != params.size())
		throw runtime_error("offerhistory <offer>\n"
				"List all stored values of an offer.\n");

	Array oRes;
	vector<unsigned char> vchOffer = vchFromValue(params[0]);
	string offer = stringFromVch(vchOffer);

	{

		//vector<CDiskTxPos> vtxPos;
		vector<COffer> vtxPos;
		//COfferDB dbOffer("r");
		if (!pofferdb->ReadOffer(vchOffer, vtxPos))
			throw JSONRPCError(RPC_WALLET_ERROR,
					"failed to read from offer DB");

		COffer txPos2;
		uint256 txHash;
		uint256 blockHash;
		BOOST_FOREACH(txPos2, vtxPos) {
			txHash = txPos2.txHash;
			CTransaction tx;
			if (!GetTransaction(txHash, tx, blockHash, true)) {
				error("could not read txpos");
				continue;
			}
			int expired = 0;
			int expires_in = 0;
			int expired_block = 0;
			Object oOffer;
			vector<unsigned char> vchValue;
			int nHeight;
			uint256 hash;
			if (GetValueOfOfferTxHash(txHash, vchValue, hash, nHeight)) {
				oOffer.push_back(Pair("offer", offer));
				string value = stringFromVch(vchValue);
				oOffer.push_back(Pair("value", value));
				oOffer.push_back(Pair("txid", tx.GetHash().GetHex()));
				expired_block = nHeight + GetOfferDisplayExpirationDepth();
				if(nHeight + GetOfferDisplayExpirationDepth() - pindexBest->nHeight <= 0)
				{
					expired = 1;
				}  
				if(expired == 0)
				{
					expires_in = nHeight + GetOfferDisplayExpirationDepth() - pindexBest->nHeight;
				}
				oOffer.push_back(Pair("expires_in", expires_in));
				oOffer.push_back(Pair("expires_on", expired_block));
				oOffer.push_back(Pair("expired", expired));
				oRes.push_back(oOffer);
			}
		}
	}
	return oRes;
}

Value offerfilter(const Array& params, bool fHelp) {
	if (fHelp || params.size() > 5)
		throw runtime_error(
				"offerfilter [[[[[regexp] maxage=36000] from=0] nb=0] stat]\n"
						"scan and filter offeres\n"
						"[regexp] : apply [regexp] on offeres, empty means all offeres\n"
						"[maxage] : look in last [maxage] blocks\n"
						"[from] : show results from number [from]\n"
						"[nb] : show [nb] results, 0 means all\n"
						"[stats] : show some stats instead of results\n"
						"offerfilter \"\" 5 # list offeres updated in last 5 blocks\n"
						"offerfilter \"^offer\" # list all offeres starting with \"offer\"\n"
						"offerfilter 36000 0 0 stat # display stats (number of offers) on active offeres\n");

	string strRegexp;
	int nFrom = 0;
	int nNb = 0;
	int nMaxAge = 36000;
	bool fStat = false;
	int nCountFrom = 0;
	int nCountNb = 0;

	if (params.size() > 0)
		strRegexp = params[0].get_str();

	if (params.size() > 1)
		nMaxAge = params[1].get_int();

	if (params.size() > 2)
		nFrom = params[2].get_int();

	if (params.size() > 3)
		nNb = params[3].get_int();

	if (params.size() > 4)
		fStat = (params[4].get_str() == "stat" ? true : false);

	//COfferDB dbOffer("r");
	Array oRes;

	vector<unsigned char> vchOffer;
	vector<pair<vector<unsigned char>, COffer> > offerScan;
	if (!pofferdb->ScanOffers(vchOffer, 100000000, offerScan))
		throw JSONRPCError(RPC_WALLET_ERROR, "scan failed");

	pair<vector<unsigned char>, COffer> pairScan;
	BOOST_FOREACH(pairScan, offerScan) {
		COffer txOffer = pairScan.second;
        string offer = stringFromVch(txOffer.vchRand);
		string title = stringFromVch(txOffer.sTitle);
		string offerToSearch = offer;
		std::transform(offerToSearch.begin(), offerToSearch.end(), offerToSearch.begin(), ::tolower);
		std::transform(title.begin(), title.end(), title.begin(), ::tolower);
		std::transform(strRegexp.begin(), strRegexp.end(), strRegexp.begin(), ::tolower);
        // regexp
        using namespace boost::xpressive;
        smatch offerparts;
        sregex cregex = sregex::compile(strRegexp);
        if (strRegexp != "" && !regex_search(title, offerparts, cregex) && strRegexp != offerToSearch)
            continue;

		int expired = 0;
		int expires_in = 0;
		int expired_block = 0;		
		int nHeight = txOffer.nHeight;

		// max age
		if (nMaxAge != 0 && pindexBest->nHeight - nHeight >= nMaxAge)
			continue;

		// from limits
		nCountFrom++;
		if (nCountFrom < nFrom + 1)
			continue;
        CTransaction tx;
        uint256 blockHash;
		if (!GetTransaction(txOffer.txHash, tx, blockHash, true))
			continue;

		Object oOffer;
		oOffer.push_back(Pair("offer", offer));
        oOffer.push_back(Pair("title", stringFromVch(txOffer.sTitle)));
		oOffer.push_back(Pair("description", stringFromVch(txOffer.sDescription)));
        oOffer.push_back(Pair("category", stringFromVch(txOffer.sCategory)));
		int precision = 2;
		convertCurrencyCodeToSyscoin(txOffer.sCurrencyCode, 0, nBestHeight, precision);
		oOffer.push_back(Pair("price", strprintf("%.*f", precision, txOffer.GetPrice() ))); 	
		oOffer.push_back(Pair("currency", stringFromVch(txOffer.sCurrencyCode)));
		oOffer.push_back(Pair("commission", strprintf("%d%%", txOffer.nCommission)));
        oOffer.push_back(Pair("quantity", strprintf("%u", txOffer.nQty)));
		oOffer.push_back(Pair("exclusive_resell", txOffer.linkWhitelist.bExclusiveResell ? "ON" : "OFF"));
		expired_block = nHeight + GetOfferDisplayExpirationDepth();
		if(nHeight + GetOfferDisplayExpirationDepth() - pindexBest->nHeight <= 0)
		{
			expired = 1;
		}  
		if(expired == 0)
		{
			expires_in = nHeight + GetOfferDisplayExpirationDepth() - pindexBest->nHeight;
		}
		oOffer.push_back(Pair("expires_in", expires_in));
		oOffer.push_back(Pair("expires_on", expired_block));
		oOffer.push_back(Pair("expired", expired));
		oRes.push_back(oOffer);

		nCountNb++;
		// nb limits
		if (nNb > 0 && nCountNb >= nNb)
			break;
	}

	if (fStat) {
		Object oStat;
		oStat.push_back(Pair("blocks", (int) nBestHeight));
		oStat.push_back(Pair("count", (int) oRes.size()));
		//oStat.push_back(Pair("sha256sum", SHA256(oRes), true));
		return oStat;
	}

	return oRes;
}

Value offerscan(const Array& params, bool fHelp) {
	if (fHelp || 2 > params.size())
		throw runtime_error(
				"offerscan [<start-offer>] [<max-returned>]\n"
						"scan all offers, starting at start-offer and returning a maximum number of entries (default 500)\n");

	vector<unsigned char> vchOffer;
	int nMax = 500;
	if (params.size() > 0) {
		vchOffer = vchFromValue(params[0]);
	}

	if (params.size() > 1) {
		Value vMax = params[1];
		ConvertTo<double>(vMax);
		nMax = (int) vMax.get_real();
	}

	//COfferDB dbOffer("r");
	Array oRes;

	vector<pair<vector<unsigned char>, COffer> > offerScan;
	if (!pofferdb->ScanOffers(vchOffer, nMax, offerScan))
		throw JSONRPCError(RPC_WALLET_ERROR, "scan failed");

	pair<vector<unsigned char>, COffer> pairScan;
	BOOST_FOREACH(pairScan, offerScan) {
		int expired = 0;
		int expires_in = 0;
		int expired_block = 0;
		Object oOffer;
		string offer = stringFromVch(pairScan.first);
		oOffer.push_back(Pair("offer", offer));
		CTransaction tx;
		COffer txOffer = pairScan.second;
		uint256 blockHash;

		int nHeight = txOffer.nHeight;
		expired_block = nHeight + GetOfferDisplayExpirationDepth();
		if(nHeight + GetOfferDisplayExpirationDepth() - pindexBest->nHeight <= 0)
		{
			expired = 1;
		}  
		if(expired == 0)
		{
			expires_in = nHeight + GetOfferDisplayExpirationDepth() - pindexBest->nHeight;
		}
		oOffer.push_back(Pair("expires_in", expires_in));
		oOffer.push_back(Pair("expires_on", expired_block));
		oOffer.push_back(Pair("expired", expired));
		oRes.push_back(oOffer);
	}

	return oRes;
}

