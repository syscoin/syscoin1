#include "cert.h"
#include "init.h"
#include "txdb.h"
#include "util.h"
#include "auxpow.h"
#include "script.h"
#include "main.h"

#include "json/json_spirit_reader_template.h"
#include "json/json_spirit_writer_template.h"

#include <boost/xpressive/xpressive_dynamic.hpp>

using namespace std;
using namespace json_spirit;

template<typename T> void ConvertTo(Value& value, bool fAllowNull = false);

std::map<std::vector<unsigned char>, uint256> mapMyCerts;
std::map<std::vector<unsigned char>, uint256> mapMyCerts;
std::map<std::vector<unsigned char>, std::set<uint256> > mapCertPending;
std::map<std::vector<unsigned char>, std::set<uint256> > mapCertPending;
std::list<CCertFee> lstCertFees;
vector<vector<unsigned char> > vecCertIndex;

#ifdef GUI
extern std::map<uint160, std::vector<unsigned char> > mapMyCertHashes;
#endif

extern CCertDB *pcertdb;

extern uint256 SignatureHash(CScript scriptCode, const CTransaction& txTo,
		unsigned int nIn, int nHashType);

CScript RemoveCertScriptPrefix(const CScript& scriptIn);
bool DecodeCertScript(const CScript& script, int& op,
		std::vector<std::vector<unsigned char> > &vvch,
		CScript::const_iterator& pc);

extern bool Solver(const CKeyStore& keystore, const CScript& scriptPubKey,
		uint256 hash, int nHashType, CScript& scriptSigRet,
		txnouttype& whichTypeRet);
extern bool VerifyScript(const CScript& scriptSig, const CScript& scriptPubKey,
		const CTransaction& txTo, unsigned int nIn, unsigned int flags,
		int nHashType);

bool IsCertOp(int op) {
	return op == OP_CERTISSUER_NEW
			|| op == OP_CERTISSUER_ACTIVATE
			|| op == OP_CERTISSUER_UPDATE
			|| op == OP_CERT_NEW
			|| op == OP_CERT_TRANSFER;
}

// 10080 blocks = 1 week
// cert expiration time is ~ 2 weeks
// expiration blocks is 20160 (final)
// expiration starts at 6720, increases by 1 per block starting at
// block 13440 until block 349440

int nStartHeight = 161280;

int64 GetCertNetworkFee(int seed, int nHeight) {
	int nComputedHeight = nHeight - nStartHeight < 0 ? 1 : ( nHeight - nStartHeight ) + 1;
    if (nComputedHeight >= 13440) nComputedHeight += (nComputedHeight - 13440) * 3;
    if ((nComputedHeight >> 13) >= 60) return 0;
    int64 nStart = seed * COIN;
    if (fTestNet) nStart = 10 * CENT;
    int64 nRes = nStart >> (nComputedHeight >> 13);
    nRes -= (nRes >> 14) * (nComputedHeight % 8192);
    nRes += CENT - 1;
	nRes = (nRes / CENT) * CENT;
    return nRes;
}

// Increase expiration to 36000 gradually starting at block 24000.
// Use for validation purposes and pass the chain height.
int GetCertIssuerExpirationDepth(int nHeight) {
	int nComputedHeight = ( nHeight - nStartHeight < 0 ) ? 1 : ( nHeight - nStartHeight ) + 1;
    if (nComputedHeight < 13440) return 6720;
    if (nComputedHeight < 26880) return nComputedHeight - 6720;
    return 20160;
}

// For display purposes, pass the name height.
int GetCertIssuerDisplayExpirationDepth(int nHeight) {
	int nComputedHeight = nHeight - nStartHeight < 0 ? 1 : ( nHeight - nStartHeight ) + 1;
    if (nComputedHeight < 6720) return 6720;
    return 20160;
}

bool IsMyCert(const CTransaction& tx, const CTxOut& txout) {
	const CScript& scriptPubKey = RemoveCertScriptPrefix(txout.scriptPubKey);
	CScript scriptSig;
	txnouttype whichTypeRet;
	if (!Solver(*pwalletMain, scriptPubKey, 0, 0, scriptSig, whichTypeRet))
		return false;
    return true;
}

string certFromOp(int op) {
	switch (op) {
	case OP_CERTISSUER_NEW:
		return "certissuernew";
	case OP_CERTISSUER_ACTIVATE:
		return "certissueractivate";
	case OP_CERTISSUER_UPDATE:
		return "certissuerupdate";
	case OP_CERT_NEW:
		return "certissuernew";
	case OP_CERT_TRANSFER:
		return "certtransfer";
	default:
		return "<unknown cert op>";
	}
}

bool CCertIssuer::UnserializeFromTx(const CTransaction &tx) {
	try {
		CDataStream dsCert(vchFromString(DecodeBase64(stringFromVch(tx.data))), SER_NETWORK, PROTOCOL_VERSION);
		dsCert >> *this;
	} catch (std::exception &e) {
		return false;
	}
	return true;
}

void CCertIssuer::SerializeToTx(CTransaction &tx) {
	vector<unsigned char> vchData = vchFromString(SerializeToString());
	tx.data = vchData;
}

string CCertIssuer::SerializeToString() {
	// serialize cert object
	CDataStream dsCert(SER_NETWORK, PROTOCOL_VERSION);
	dsCert << *this;
	vector<unsigned char> vchData(dsCert.begin(), dsCert.end());
	return EncodeBase64(vchData.data(), vchData.size());
}

//TODO implement
bool CCertDB::ScanCerts(const std::vector<unsigned char>& vchCertIssuer, int nMax,
		std::vector<std::pair<std::vector<unsigned char>, CCertIssuer> >& certScan) {
	return true;
}

/**
 * [CCertDB::ReconstructCertIndex description]
 * @param  pindexRescan [description]
 * @return              [description]
 */
bool CCertDB::ReconstructCertIndex(CBlockIndex *pindexRescan) {
    CBlockIndex* pindex = pindexRescan;
    
    {
	LOCK(pwalletMain->cs_wallet);
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

            // decode the cert op, params, height
            bool o = DecodeCertTx(tx, op, nOut, vvchArgs, nHeight);
            if (!o || !IsCertOp(op)) continue;
            
            if (op == OP_CERTISSUER_NEW) continue;

            vector<unsigned char> vchCertIssuer = vvchArgs[0];
        
            // get the transaction
            if(!GetTransaction(tx.GetHash(), tx, txblkhash, true))
                continue;

            // attempt to read cert from txn
            CCertIssuer txCertIssuer;
            CCert txCA;
            if(!txCertIssuer.UnserializeFromTx(tx))
				return error("ReconstructCertIndex() : failed to read cert from tx");

            // read cert from DB if it exists
            vector<CCertIssuer> vtxPos;
            if (ExistsCert(vchCertIssuer)) {
                if (!ReadCert(vchCertIssuer, vtxPos))
                    return error("ReconstructCertIndex() : failed to read cert from DB");
                if(vtxPos.size()!=0) {
                	txCertIssuer.nHeight = nHeight;
                	txCertIssuer.GetCertIssuerFromList(vtxPos);
                }
            }

            // read the cert accept from db if exists
            if(op == OP_CERT_NEW || op == OP_CERT_TRANSFER) {
            	bool bReadCert = false;
            	vector<unsigned char> vchCert = vvchArgs[1];
	            if (ExistsCert(vchCert)) {
	                if (!ReadCert(vchCert, vchCertIssuer))
	                    printf("ReconstructCertIndex() : warning - failed to read cert accept from cert DB\n");
	                else bReadCert = true;
	            }
				if(!bReadCert && !txCertIssuer.GetAcceptByHash(vchCert, txCA))
					printf("ReconstructCertIndex() : failed to read cert accept from cert\n");

				// add txn-specific values to cert accept object
		        txCA.nTime = pindex->nTime;
		        txCA.txHash = tx.GetHash();
		        txCA.nHeight = nHeight;
				txCertIssuer.PutCert(txCA);
			}

			// txn-specific values to cert object
			txCertIssuer.txHash = tx.GetHash();
            txCertIssuer.nHeight = nHeight;
            txCertIssuer.PutToIssuerList(vtxPos);

            if (!WriteCert(vchCertIssuer, vtxPos))
                return error("ReconstructCertIndex() : failed to write to cert DB");
            if(op == OP_CERT_NEW || op == OP_CERT_TRANSFER)
	            if (!WriteCert(vvchArgs[1], vvchArgs[0]))
	                return error("ReconstructCertIndex() : failed to write to cert DB");
			
			// insert certs fees to regenerate list, write cert to
			// master index
			int64 nTheFee = GetCertNetFee(tx);
			InsertCertFee(pindex, tx.GetHash(), nTheFee);
			vecCertIndex.push_back(vvchArgs[0]);
	        if (!pcertdb->WriteCertIndex(vecCertIndex))
	            return error("ReconstructCertIndex() : failed to write index to cert DB");

            if(IsCertTxnMine(tx)) {
                if(op == OP_CERT_NEW || op == OP_CERT_TRANSFER) {
                    mapMyCerts[vvchArgs[1]] = tx.GetHash();
                    if(mapMyCerts.count(vchCertIssuer))
                        mapMyCerts[vchCertIssuer] = tx.GetHash();
                }
                else mapMyCerts[vchCertIssuer] = tx.GetHash();
            }

			printf( "RECONSTRUCT CERT: op=%s cert=%s title=%s qty=%d hash=%s height=%d fees=%llu\n",
					certFromOp(op).c_str(),
					stringFromVch(vvchArgs[0]).c_str(),
					stringFromVch(txCertIssuer.sTitle).c_str(),
					txCertIssuer.GetRemQty(),
					tx.GetHash().ToString().c_str(), 
					nHeight,
					nTheFee);	            
        }
        pindex = pindex->pnext;
        Flush();
    }
    }
    return true;
}

// get the depth of transaction txnindex relative to block at index pIndexBlock, looking
// up to maxdepth. Return relative depth if found, or -1 if not found and maxdepth reached.
int CheckCertTxnTransactionAtRelativeDepth(CBlockIndex* pindexBlock,
		const CCoins *txindex, int maxDepth) {
	for (CBlockIndex* pindex = pindexBlock;
			pindex && pindexBlock->nHeight - pindex->nHeight < maxDepth;
			pindex = pindex->pprev)
		if (pindex->nHeight == (int) txindex->nHeight)
			return pindexBlock->nHeight - pindex->nHeight;
	return -1;
}

int GetCertTxHashHeight(const uint256 txHash) {
	CDiskTxPos postx;
	pblocktree->ReadTxIndex(txHash, postx);
	return postx.nPos;
}

uint64 GetCertTxnFeeSubsidy(unsigned int nHeight) {
	vector<CCertFee> vo;
	pcertdb->ReadCertTxFees(vo);
	list<CCertFee> lOF(vo.begin(), vo.end());
	lstCertFees = lOF;

	unsigned int h12 = 360 * 12;
	unsigned int nTargetTime = 0;
	unsigned int nTarget1hrTime = 0;
	unsigned int blk1hrht = nHeight - 1;
	unsigned int blk12hrht = nHeight - 1;
	bool bFound = false;
	uint64 hr1 = 1, hr12 = 1;

	BOOST_FOREACH(CCertFee &nmFee, lstCertFees) {
		if(nmFee.nHeight <= nHeight)
			bFound = true;
		if(bFound) {
			if(nTargetTime==0) {
				hr1 = hr12 = 0;
				nTargetTime = nmFee.nTime - h12;
				nTarget1hrTime = nmFee.nTime - (h12/12);
			}
			if(nmFee.nTime > nTargetTime) {
				hr12 += nmFee.nFee;
				blk12hrht = nmFee.nHeight;
				if(nmFee.nTime > nTarget1hrTime) {
					hr1 += nmFee.nFee;
					blk1hrht = nmFee.nHeight;
				}
			}
		}
	}
	hr12 /= (nHeight - blk12hrht) + 1;
	hr1 /= (nHeight - blk1hrht) + 1;
	uint64 nSubsidyOut = hr1 > hr12 ? hr1 : hr12;
	return nSubsidyOut;
}

bool InsertCertFee(CBlockIndex *pindex, uint256 hash, uint64 nValue) {
	unsigned int h12 = 3600 * 12;
	list<CCertFee> txnDup;
	CCertFee oFee;
	oFee.nTime = pindex->nTime;
	oFee.nHeight = pindex->nHeight;
	oFee.nFee = nValue;
	bool bFound = false;
	
	unsigned int tHeight =
			pindex->nHeight - 2880 < 0 ? 0 : pindex->nHeight - 2880;
	
	while (true) {
		if (lstCertFees.size() > 0
				&& (lstCertFees.back().nTime + h12 < pindex->nTime
						|| lstCertFees.back().nHeight < tHeight))
			lstCertFees.pop_back();
		else
			break;
	}
	BOOST_FOREACH(CCertFee &nmFee, lstCertFees) {
		if (oFee.hash == nmFee.hash
				&& oFee.nHeight == nmFee.nHeight) {
			bFound = true;
			break;
		}
	}
	if (!bFound)
		lstCertFees.push_front(oFee);

	return true;
}

int64 GetCertNetFee(const CTransaction& tx) {
	int64 nFee = 0;
	for (unsigned int i = 0; i < tx.vout.size(); i++) {
		const CTxOut& out = tx.vout[i];
		if (out.scriptPubKey.size() == 1 && out.scriptPubKey[0] == OP_RETURN)
			nFee += out.nValue;
	}
	return nFee;
}

int GetCertIssuerHeight(vector<unsigned char> vchCertIssuer) {
	vector<CCertIssuer> vtxPos;
	if (pcertdb->ExistsCert(vchCertIssuer)) {
		if (!pcertdb->ReadCert(vchCertIssuer, vtxPos))
			return error("GetCertIssuerHeight() : failed to read from cert DB");
		if (vtxPos.empty()) return -1;
		CCertIssuer& txPos = vtxPos.back();
		return txPos.nHeight;
	}
	return -1;
}

// Check that the last entry in cert history matches the given tx pos
bool CheckCertTxnTxPos(const vector<CCertIssuer> &vtxPos, const int txPos) {
	if (vtxPos.empty()) return false;
	CCertIssuer cert;
	cert.nHeight = txPos;
	return cert.GetCertIssuerFromList(vtxPos);
}

int IndexOfCertOutput(const CTransaction& tx) {
	vector<vector<unsigned char> > vvch;
	int op, nOut;
	if (!DecodeCertTx(tx, op, nOut, vvch, -1))
		throw runtime_error("IndexOfCertOutput() : cert output not found");
	return nOut;
}

bool GetNameOfCertTx(const CTransaction& tx, vector<unsigned char>& cert) {
	if (tx.nVersion != SYSCOIN_TX_VERSION)
		return false;
	vector<vector<unsigned char> > vvchArgs;
	int op, nOut;
	if (!DecodeCertTx(tx, op, nOut, vvchArgs, -1))
		return error("GetNameOfCertTx() : could not decode a syscoin tx");

	switch (op) {
		case OP_CERT_NEW:
		case OP_CERTISSUER_ACTIVATE:
		case OP_CERTISSUER_UPDATE:
		case OP_CERT_TRANSFER:
			cert = vvchArgs[0];
			return true;
	}
	return false;
}

//TODO come back here check to see how / where this is used
bool IsConflictedCertTx(CBlockTreeDB& txdb, const CTransaction& tx,
		vector<unsigned char>& cert) {
	if (tx.nVersion != SYSCOIN_TX_VERSION)
		return false;
	vector<vector<unsigned char> > vvchArgs;
	int op, nOut, nPrevHeight;
	if (!DecodeCertTx(tx, op, nOut, vvchArgs, pindexBest->nHeight))
		return error("IsConflictedCertTx() : could not decode a syscoin tx");

	switch (op) {
	case OP_CERTISSUER_UPDATE:
		nPrevHeight = GetCertIssuerHeight(vvchArgs[0]);
		cert = vvchArgs[0];
		if (nPrevHeight >= 0
				&& pindexBest->nHeight - nPrevHeight
						< GetCertIssuerExpirationDepth(pindexBest->nHeight))
			return true;
	}
	return false;
}

bool GetValueOfCertTx(const CTransaction& tx, vector<unsigned char>& value) {
	vector<vector<unsigned char> > vvch;
	int op, nOut;

	if (!DecodeCertTx(tx, op, nOut, vvch, -1))
		return false;

	switch (op) {
	case OP_CERTISSUER_NEW:
		return false;
	case OP_CERTISSUER_ACTIVATE:
	case OP_CERT_NEW:
		value = vvch[2];
		return true;
	case OP_CERTISSUER_UPDATE:
	case OP_CERT_TRANSFER:
		value = vvch[1];
		return true;
	default:
		return false;
	}
}

bool IsCertTxnMine(const CTransaction& tx) {
	if (tx.nVersion != SYSCOIN_TX_VERSION)
		return false;

	vector<vector<unsigned char> > vvch;
	int op, nOut;

	bool good = DecodeCertTx(tx, op, nOut, vvch, -1);
	if (!good) {
		error( "IsCertTxnMine() : no output out script in cert tx %s\n",
				tx.ToString().c_str());
		return false;
	}
	if(!IsCertOp(op))
		return false;

	const CTxOut& txout = tx.vout[nOut];
	if (IsMyCert(tx, txout)) {
		printf("IsCertTxnMine() : found my transaction %s nout %d\n",
				tx.GetHash().GetHex().c_str(), nOut);
		return true;
	}
	return false;
}

bool IsCertTxnMine(const CTransaction& tx, const CTxOut& txout,
		bool ignore_certissuernew) {
	if (tx.nVersion != SYSCOIN_TX_VERSION)
		return false;

	vector<vector<unsigned char> > vvch;
	int op;

	if (!DecodeCertScript(txout.scriptPubKey, op, vvch))
		return false;

	if(!IsCertOp(op))
		return false;

	if (ignore_certissuernew && op == OP_CERTISSUER_NEW)
		return false;

	if (IsMyCert(tx, txout)) {
		printf("IsCertTxnMine() : found my transaction %s value %d\n",
				tx.GetHash().GetHex().c_str(), (int) txout.nValue);
		return true;
	}
	return false;
}

bool GetValueOfCertTxHash(const uint256 &txHash,
		vector<unsigned char>& vchValue, uint256& hash, int& nHeight) {
	nHeight = GetCertTxHashHeight(txHash);
	CTransaction tx;
	uint256 blockHash;
	if (!GetTransaction(txHash, tx, blockHash, true))
		return error("GetValueOfCertTxHash() : could not read tx from disk");
	if (!GetValueOfCertTx(tx, vchValue))
		return error("GetValueOfCertTxHash() : could not decode value from tx");
	hash = tx.GetHash();
	return true;
}

bool GetValueOfCert(CCertDB& dbCert, const vector<unsigned char> &vchCertIssuer,
		vector<unsigned char>& vchValue, int& nHeight) {
	vector<CCertIssuer> vtxPos;
	if (!pcertdb->ReadCert(vchCertIssuer, vtxPos) || vtxPos.empty())
		return false;

	CCertIssuer& txPos = vtxPos.back();
	nHeight = txPos.nHeight;
	vchValue = txPos.vchRand;
	return true;
}

bool GetTxOfCert(CCertDB& dbCert, const vector<unsigned char> &vchCertIssuer,
		CTransaction& tx) {
	vector<CCertIssuer> vtxPos;
	if (!pcertdb->ReadCert(vchCertIssuer, vtxPos) || vtxPos.empty())
		return false;
	CCertIssuer& txPos = vtxPos.back();
	int nHeight = txPos.nHeight;
	if (nHeight + GetCertIssuerExpirationDepth(pindexBest->nHeight)
			< pindexBest->nHeight) {
		string cert = stringFromVch(vchCertIssuer);
		printf("GetTxOfCert(%s) : expired", cert.c_str());
		return false;
	}

	uint256 hashBlock;
	if (!GetTransaction(txPos.txHash, tx, hashBlock, true))
		return error("GetTxOfCert() : could not read tx from disk");

	return true;
}

bool GetTxOfCert(CCertDB& dbCert, const vector<unsigned char> &vchCert,
		CCertIssuer &txPos, CTransaction& tx) {
	vector<CCertIssuer> vtxPos;
	vector<unsigned char> vchCertIssuer;
	if (!pcertdb->ReadCert(vchCert, vchCertIssuer)) return false;
	if (!pcertdb->ReadCert(vchCertIssuer, vtxPos)) return false;
	txPos = vtxPos.back();
	int nHeight = txPos.nHeight;
	if (nHeight + GetCertIssuerExpirationDepth(pindexBest->nHeight)
			< pindexBest->nHeight) {
		string cert = stringFromVch(vchCert);
		printf("GetTxOfCert(%s) : expired", cert.c_str());
		return false;
	}

	uint256 hashBlock;
	if (!GetTransaction(txPos.txHash, tx, hashBlock, true))
		return error("GetTxOfCert() : could not read tx from disk");

	return true;
}

bool DecodeCertTx(const CTransaction& tx, int& op, int& nOut,
		vector<vector<unsigned char> >& vvch, int nHeight) {
	bool found = false;

	if (nHeight < 0)
		nHeight = pindexBest->nHeight;

	// Strict check - bug disallowed
	for (unsigned int i = 0; i < tx.vout.size(); i++) {
		const CTxOut& out = tx.vout[i];
		vector<vector<unsigned char> > vvchRead;
		if (DecodeCertScript(out.scriptPubKey, op, vvchRead)) {
			nOut = i; found = true; vvch = vvchRead;
			break;
		}
	}
	if (!found) vvch.clear();
	return found && IsCertOp(op);
}

bool GetValueOfCertTx(const CCoins& tx, vector<unsigned char>& value) {
	vector<vector<unsigned char> > vvch;

	int op, nOut;

	if (!DecodeCertTx(tx, op, nOut, vvch, -1))
		return false;

	switch (op) {
	case OP_CERTISSUER_NEW:
		return false;
	case OP_CERTISSUER_ACTIVATE:
	case OP_CERT_NEW:
		value = vvch[2];
		return true;
	case OP_CERTISSUER_UPDATE:
	case OP_CERT_TRANSFER:
		value = vvch[1];
		return true;
	default:
		return false;
	}
}

bool DecodeCertTx(const CCoins& tx, int& op, int& nOut,
		vector<vector<unsigned char> >& vvch, int nHeight) {
	bool found = false;

	if (nHeight < 0)
		nHeight = pindexBest->nHeight;

	// Strict check - bug disallowed
	for (unsigned int i = 0; i < tx.vout.size(); i++) {
		const CTxOut& out = tx.vout[i];
		vector<vector<unsigned char> > vvchRead;
		if (DecodeCertScript(out.scriptPubKey, op, vvchRead)) {
			nOut = i; found = true; vvch = vvchRead;
			break;
		}
	}
	if (!found)
		vvch.clear();
	return found;
}

bool DecodeCertScript(const CScript& script, int& op,
		vector<vector<unsigned char> > &vvch) {
	CScript::const_iterator pc = script.begin();
	return DecodeCertScript(script, op, vvch, pc);
}

bool DecodeCertScript(const CScript& script, int& op,
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

	if ((op == OP_CERTISSUER_NEW && vvch.size() == 1)
		|| (op == OP_CERTISSUER_ACTIVATE && vvch.size() == 3)
		|| (op == OP_CERTISSUER_UPDATE && vvch.size() == 2)
		|| (op == OP_CERT_NEW && vvch.size() == 3)
		|| (op == OP_CERT_TRANSFER && vvch.size() == 2))
		return true;
	return false;
}

bool SignCertSignature(const CTransaction& txFrom, CTransaction& txTo,
		unsigned int nIn, int nHashType = SIGHASH_ALL, CScript scriptPrereq =
				CScript()) {
	assert(nIn < txTo.vin.size());
	CTxIn& txin = txTo.vin[nIn];
	assert(txin.prevout.n < txFrom.vout.size());
	const CTxOut& txout = txFrom.vout[txin.prevout.n];

	// Leave out the signature from the hash, since a signature can't sign itself.
	// The checksig op will also drop the signatures from its hash.
	const CScript& scriptPubKey = RemoveCertScriptPrefix(txout.scriptPubKey);
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

bool CreateCertTransactionWithInputTx(
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
	{
		LOCK2(cs_main, pwalletMain->cs_wallet);

		nFeeRet = nTransactionFee;
		loop {
			wtxNew.vin.clear();
			wtxNew.vout.clear();
			wtxNew.fFromMe = true;
			wtxNew.data = vchFromString(txData);

			int64 nTotalValue = nValue + nFeeRet;
			printf("CreateCertTransactionWithInputTx: total value = %d\n",
					(int) nTotalValue);
			double dPriority = 0;

			// vouts to the payees
			BOOST_FOREACH(const PAIRTYPE(CScript, int64)& s, vecSend)
				wtxNew.vout.push_back(CTxOut(s.second, s.first));

			int64 nWtxinCredit = wtxIn.vout[nTxOut].nValue;

			// Choose coins to use
			set<pair<const CWalletTx*, unsigned int> > setCoins;
			int64 nValueIn = 0;
			printf( "CreateCertTransactionWithInputTx: SelectCoins(%s), nTotalValue = %s, nWtxinCredit = %s\n",
					FormatMoney(nTotalValue - nWtxinCredit).c_str(),
					FormatMoney(nTotalValue).c_str(),
					FormatMoney(nWtxinCredit).c_str());
			if (nTotalValue - nWtxinCredit > 0) {
				if (!pwalletMain->SelectCoins(nTotalValue - nWtxinCredit,
						setCoins, nValueIn))
					return false;
			}

			printf( "CreateCertTransactionWithInputTx: selected %d tx outs, nValueIn = %s\n",
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
					if (!SignCertSignature(*coin.first, wtxNew, nIn++))
						throw runtime_error("could not sign cert coin output");
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
				printf( "CreateCertTransactionWithInputTx: re-iterating (nFreeRet = %s)\n",
						FormatMoney(nFeeRet).c_str());
				continue;
			}

			// Fill vtxPrev by copying from previous transactions vtxPrev
			wtxNew.AddSupportingTransactions();
			wtxNew.fTimeReceivedIsTxTime = true;

			break;
		}
	}

	printf("CreateCertTransactionWithInputTx succeeded:\n%s",
			wtxNew.ToString().c_str());
	return true;
}

int64 GetFeeAssign() {
	int64 iRet = !0;
	return  iRet<<47;
}

// nTxOut is the output from wtxIn that we should grab
string SendCertMoneyWithInputTx(CScript scriptPubKey, int64 nValue,
		int64 nNetFee, CWalletTx& wtxIn, CWalletTx& wtxNew, bool fAskFee,
		const string& txData) {
	int nTxOut = IndexOfCertOutput(wtxIn);
	CReserveKey reservekey(pwalletMain);
	int64 nFeeRequired;
	vector<pair<CScript, int64> > vecSend;
	vecSend.push_back(make_pair(scriptPubKey, nValue));

	if (nNetFee) {
		CScript scriptFee;
		scriptFee << OP_RETURN;
		vecSend.push_back(make_pair(scriptFee, nNetFee));
	}

	if (!CreateCertTransactionWithInputTx(vecSend, wtxIn, nTxOut, wtxNew,
			reservekey, nFeeRequired, txData)) {
		string strError;
		if (nValue + nFeeRequired > pwalletMain->GetBalance())
			strError = strprintf(_("Error: This transaction requires a transaction fee of at least %s because of its amount, complexity, or use of recently received funds "),
							FormatMoney(nFeeRequired).c_str());
		else
			strError = _("Error: Transaction creation failed  ");
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
		return _(
				"Error: The transaction was rejected.  This might happen if some of the coins in your wallet were already spent, such as if you used a copy of wallet.dat and coins were spent in the copy but not marked as spent here.");

	return "";
}

bool GetCertIssuerAddress(const CTransaction& tx, std::string& strAddress) {
	int op, nOut = 0;
	vector<vector<unsigned char> > vvch;

	if (!DecodeCertTx(tx, op, nOut, vvch, -1))
		return error("GetCertIssuerAddress() : could not decode cert tx.");

	const CTxOut& txout = tx.vout[nOut];

	const CScript& scriptPubKey = RemoveCertScriptPrefix(txout.scriptPubKey);
	strAddress = CBitcoinAddress(scriptPubKey.GetID()).ToString();
	return true;
}

bool GetCertIssuerAddress(const CDiskTxPos& txPos, std::string& strAddress) {
	CTransaction tx;
	if (!tx.ReadFromDisk(txPos))
		return error("GetCertIssuerAddress() : could not read tx from disk");
	return GetCertIssuerAddress(tx, strAddress);
}

CScript RemoveCertScriptPrefix(const CScript& scriptIn) {
	int op;
	vector<vector<unsigned char> > vvch;
	CScript::const_iterator pc = scriptIn.begin();

	if (!DecodeCertScript(scriptIn, op, vvch, pc))
		throw runtime_error(
				"RemoveCertScriptPrefix() : could not decode cert script");
	return CScript(pc, scriptIn.end());
}

bool CheckCertTxnInputs(CBlockIndex *pindexBlock, const CTransaction &tx,
		CValidationState &state, CCoinsViewCache &inputs,
		map<uint256, uint256> &mapTestPool, bool fBlock, bool fMiner,
		bool fJustCheck) {

	if (!tx.IsCoinBase()) {
		printf("*** %d %d %s %s %s %s\n", pindexBlock->nHeight,
				pindexBest->nHeight, tx.GetHash().ToString().c_str(),
				fBlock ? "BLOCK" : "", fMiner ? "MINER" : "",
				fJustCheck ? "JUSTCHECK" : "");

		bool found = false;
		const COutPoint *prevOutput = NULL;
		const CCoins *prevCoins = NULL;
		int prevOp;
		vector<vector<unsigned char> > vvchPrevArgs;

		// Strict check - bug disallowed
		for (int i = 0; i < (int) tx.vin.size(); i++) {
			prevOutput = &tx.vin[i].prevout;
			prevCoins = &inputs.GetCoins(prevOutput->hash);
			vector<vector<unsigned char> > vvch;
			if (DecodeCertScript(prevCoins->vout[prevOutput->n].scriptPubKey,
					prevOp, vvch)) {
				found = true; vvchPrevArgs = vvch;
				break;
			}
			if(!found)vvchPrevArgs.clear();
		}

		// Make sure cert outputs are not spent by a regular transaction, or the cert would be lost
		if (tx.nVersion != SYSCOIN_TX_VERSION) {
			if (found)
				return error(
						"CheckCertTxnInputs() : a non-syscoin transaction with a syscoin input");
			return true;
		}

		vector<vector<unsigned char> > vvchArgs;
		int op;
		int nOut;
		bool good = DecodeCertTx(tx, op, nOut, vvchArgs, pindexBlock->nHeight);
		if (!good)
			return error("CheckCertTxnInputs() : could not decode an certcoin tx");
		int nPrevHeight;
		int nDepth;
		int64 nNetFee;

		// unserialize cert object from txn, check for valid
		CCertIssuer theCert;
		CCert theCert;
		theCert.UnserializeFromTx(tx);
		if (theCert.IsNull())
			error("CheckCertTxnInputs() : null cert object");

		if (vvchArgs[0].size() > MAX_NAME_LENGTH)
			return error("cert hex rand too long");

		switch (op) {
		case OP_CERTISSUER_NEW:

			if (found)
				return error(
						"CheckCertTxnInputs() : certissuernew tx pointing to previous syscoin tx");

			if (vvchArgs[0].size() != 20)
				return error("certissuernew tx with incorrect hash length");

			break;

		case OP_CERTISSUER_ACTIVATE:

			// check for enough fees
			nNetFee = GetCertNetFee(tx);
			if (nNetFee < GetCertNetworkFee(8, pindexBlock->nHeight)-COIN)
				return error(
						"CheckCertTxnInputs() : got tx %s with fee too low %lu",
						tx.GetHash().GetHex().c_str(),
						(long unsigned int) nNetFee);

			// validate conditions
			if ((!found || prevOp != OP_CERTISSUER_NEW) && !fJustCheck)
				return error("CheckCertTxnInputs() : certactivate tx without previous certissuernew tx");

			if (vvchArgs[1].size() > 20)
				return error("certactivate tx with rand too big");

			if (vvchArgs[2].size() > MAX_VALUE_LENGTH)
				return error("certactivate tx with value too long");

			if (fBlock && !fJustCheck) {
				// Check hash
				const vector<unsigned char> &vchHash = vvchPrevArgs[0];
				const vector<unsigned char> &vchCertIssuer = vvchArgs[0];
				const vector<unsigned char> &vchRand = vvchArgs[1];
				vector<unsigned char> vchToHash(vchRand);
				vchToHash.insert(vchToHash.end(), vchCertIssuer.begin(), vchCertIssuer.end());
				uint160 hash = Hash160(vchToHash);

				if (uint160(vchHash) != hash)
					return error(
							"CheckCertTxnInputs() : certactivate hash mismatch prev : %s cur %s",
							HexStr(stringFromVch(vchHash)).c_str(), HexStr(stringFromVch(vchToHash)).c_str());

				// min activation depth is 1
				nDepth = CheckCertTxnTransactionAtRelativeDepth(pindexBlock,
						prevCoins, 1);
				if ((fBlock || fMiner) && nDepth >= 0 && (unsigned int) nDepth < 1)
					return false;

				// check for previous certissuernew
				nDepth = CheckCertTxnTransactionAtRelativeDepth(pindexBlock,
						prevCoins,
						GetCertIssuerExpirationDepth(pindexBlock->nHeight));
				if (nDepth == -1)
					return error(
							"CheckCertTxnInputs() : certactivate cannot be mined if certissuernew is not already in chain and unexpired");

				nPrevHeight = GetCertIssuerHeight(vchCertIssuer);
				if (!fBlock && nPrevHeight >= 0
						&& pindexBlock->nHeight - nPrevHeight
								< GetCertIssuerExpirationDepth(pindexBlock->nHeight))
					return error(
							"CheckCertTxnInputs() : certactivate on an unexpired cert.");

   				set<uint256>& setPending = mapCertPending[vchCertIssuer];
                BOOST_FOREACH(const PAIRTYPE(uint256, uint256)& s, mapTestPool) {
                	if(s.second==tx.GetHash()) continue;
                    if (setPending.count(s.second)) {
                        printf("CheckInputs() : will not mine certactivate %s because it clashes with %s",
                               tx.GetHash().GetHex().c_str(),
                               s.second.GetHex().c_str());
                        return false;
                    }
                }
			}

			break;
		case OP_CERTISSUER_UPDATE:

			if (fBlock && fJustCheck && !found)
				return true;

			if ( !found || ( prevOp != OP_CERTISSUER_ACTIVATE && prevOp != OP_CERTISSUER_UPDATE ) )
				return error("certupdate previous op %s is invalid", certFromOp(prevOp).c_str());
			
			if (vvchArgs[1].size() > MAX_VALUE_LENGTH)
				return error("certupdate tx with value too long");
			
			if (vvchPrevArgs[0] != vvchArgs[0])
				return error("CheckCertTxnInputs() : certupdate cert mismatch");

			// TODO CPU intensive
			nDepth = CheckCertTxnTransactionAtRelativeDepth(pindexBlock,
					prevCoins, GetCertIssuerExpirationDepth(pindexBlock->nHeight));
			if ((fBlock || fMiner) && nDepth < 0)
				return error(
						"CheckCertTxnInputs() : certupdate on an expired cert, or there is a pending transaction on the cert");
			
			if (fBlock && !fJustCheck) {
				set<uint256>& setPending = mapCertPending[vvchArgs[0]];
	            BOOST_FOREACH(const PAIRTYPE(uint256, uint256)& s, mapTestPool) {
	            	if(s.second==tx.GetHash()) continue;
	                if (setPending.count(s.second)) {
	                    printf("CheckInputs() : will not mine certupdate %s because it clashes with %s",
	                           tx.GetHash().GetHex().c_str(),
	                           s.second.GetHex().c_str());
	                    return false;
	                }
	            }
        	}

			break;

		case OP_CERT_NEW:

			if (vvchArgs[1].size() > 20)
				return error("certaccept tx with rand too big");

			if (vvchArgs[2].size() > 20)
				return error("certaccept tx with value too long");

			if (fBlock && !fJustCheck) {
				// Check hash
				const vector<unsigned char> &vchCertIssuer = vvchArgs[0];
				const vector<unsigned char> &vchAcceptRand = vvchArgs[1];

				nPrevHeight = GetCertIssuerHeight(vchCertIssuer);

				if(!theCert.GetAcceptByHash(vchAcceptRand, theCert))
					return error("could not read accept from cert txn");

				if(theCert.vchRand != vchAcceptRand)
					return error("accept txn contains invalid txnaccept hash");
	   		}
			break;

		case OP_CERT_TRANSFER:

			// validate conditions
			if ( ( !found || prevOp != OP_CERT_NEW ) && !fJustCheck )
				return error("certpay previous op %s is invalid", certFromOp(prevOp).c_str());

			if (vvchArgs[0].size() > 20)
				return error("certpay tx with cert hash too big");

			if (vvchArgs[1].size() > 20)
				return error("certpay tx with cert accept hash too big");

			if (fBlock && !fJustCheck) {
				// Check hash
				const vector<unsigned char> &vchCertIssuer = vvchArgs[0];
				const vector<unsigned char> &vchCert = vvchArgs[1];

				// construct cert accept hash
				vector<unsigned char> vchToHash(vchCert);
				vchToHash.insert(vchToHash.end(), vchCertIssuer.begin(), vchCertIssuer.end());
				uint160 hash = Hash160(vchToHash);

				// check for previous certaccept
				nDepth = CheckCertTxnTransactionAtRelativeDepth(pindexBlock,
						prevCoins, pindexBlock->nHeight);
				if (nDepth == -1)
					return error(
							"CheckCertTxnInputs() : certpay cannot be mined if certaccept is not already in chain");

				// check cert accept hash against prev txn
				if (uint160(vvchPrevArgs[2]) != hash)
					return error(
							"CheckCertTxnInputs() : certpay prev hash mismatch : %s vs %s",
							HexStr(stringFromVch(vvchPrevArgs[2])).c_str(), HexStr(stringFromVch(vchToHash)).c_str());

				nPrevHeight = GetCertIssuerHeight(vchCertIssuer);

				if(!theCert.GetAcceptByHash(vchCert, theCert))
					return error("could not read accept from cert txn");

				// check for enough fees
				int64 expectedFee = GetCertNetworkFee(4, pindexBlock->nHeight) 
				+ ((theCert.nPrice * theCert.nQty) / 200) - COIN;
				nNetFee = GetCertNetFee(tx);
				if (nNetFee < expectedFee )
					return error(
							"CheckCertTxnInputs() : got certpay tx %s with fee too low %lu",
							tx.GetHash().GetHex().c_str(),
							(long unsigned int) nNetFee);

				if(theCert.vchRand != vchCert)
					return error("accept txn contains invalid txnaccept hash");

   				set<uint256>& setPending = mapCertPending[vchCert];
                BOOST_FOREACH(const PAIRTYPE(uint256, uint256)& s, mapTestPool) {
                	if(s.second==tx.GetHash()) continue;
                    if (setPending.count(s.second)) {
                       printf("CheckInputs() : will not mine certpay %s because it clashes with %s",
                               tx.GetHash().GetHex().c_str(),
                               s.second.GetHex().c_str());
                       return false;
                    }
                }
			}

			break;

		default:
			return error( "CheckCertTxnInputs() : cert transaction has unknown op");
		}

		// save serialized cert for later use
		CCertIssuer serializedCert = theCert;

		// if not an certissuernew, load the cert data from the DB
		vector<CCertIssuer> vtxPos;
		if(op != OP_CERTISSUER_NEW)
			if (pcertdb->ExistsCert(vvchArgs[0])) {
				if (!pcertdb->ReadCert(vvchArgs[0], vtxPos))
					return error(
							"CheckCertTxnInputs() : failed to read from cert DB");
			}

		// for certupdate or certpay check to make sure the previous txn exists and is valid
		if (!fBlock && fJustCheck && (op == OP_CERTISSUER_UPDATE || op == OP_CERT_TRANSFER)) {
			if (!CheckCertTxnTxPos(vtxPos, prevCoins->nHeight))
				return error(
						"CheckCertTxnInputs() : tx %s rejected, since previous tx (%s) is not in the cert DB\n",
						tx.GetHash().ToString().c_str(),
						prevOutput->hash.ToString().c_str());
		}

		// these ifs are problably total bullshit except for the certissuernew
		if (fBlock || (!fBlock && !fMiner && !fJustCheck)) {
			if (op != OP_CERTISSUER_NEW) {
				if (!fMiner && !fJustCheck && pindexBlock->nHeight != pindexBest->nHeight) {
					int nHeight = pindexBlock->nHeight;

					// get the latest cert from the db
                	theCert.nHeight = nHeight;
                	theCert.GetCertIssuerFromList(vtxPos);
					
					if (op == OP_CERT_NEW || op == OP_CERT_TRANSFER) {
						// get the accept out of the cert object in the txn
						if(!serializedCert.GetAcceptByHash(vvchArgs[1], theCert))
							return error("could not read accept from cert txn");

						if(op == OP_CERT_NEW) {
							// get the cert accept qty, validate
							if(theCert.nQty < 1 || theCert.nQty > theCert.GetRemQty())
								return error("invalid quantity value (nQty < 1 or nQty > remaining qty).");
						} 
						if(op == OP_CERT_TRANSFER) {
							theCert.bPaid = true;
						}

						// set the cert accept txn-dependent values and add to the txn
						theCert.vchRand = vvchArgs[1];
						theCert.txHash = tx.GetHash();
						theCert.nTime = pindexBlock->nTime;
						theCert.nHeight = nHeight;
						theCert.PutCert(theCert);

						if (!pcertdb->WriteCert(vvchArgs[1], vvchArgs[0]))
							return error( "CheckCertTxnInputs() : failed to write to cert DB");
					}
					
					// set the cert's txn-dependent values
					theCert.txHash = tx.GetHash();
					theCert.PutToIssuerList(vtxPos);

					// write cert
					if (!pcertdb->WriteCert(vvchArgs[0], vtxPos))
						return error( "CheckCertTxnInputs() : failed to write to cert DB");

					// write cert to cert index if it isn't there already
					bool bFound = false;
                    BOOST_FOREACH(vector<unsigned char> &vch, vecCertIndex) {
                        if(vch == vvchArgs[0]) {
                            bFound = true;
                            break;
                        }
                    }
                    if(!bFound) vecCertIndex.push_back(vvchArgs[0]);
                    if (!pcertdb->WriteCertIndex(vecCertIndex))
                        return error("CheckCertTxnInputs() : failed to write index to cert DB");

                    // compute verify and write fee data to DB
                    int64 nTheFee = GetCertNetFee(tx);
					InsertCertFee(pindexBlock, tx.GetHash(), nTheFee);
					if(nTheFee > 0) printf("CERT FEES: Added %lf in fees to track for regeneration.\n", (double) nTheFee / COIN);
					vector<CCertFee> vCertFees(lstCertFees.begin(), lstCertFees.end());
					if (!pcertdb->WriteCertTxFees(vCertFees))
						return error( "CheckCertTxnInputs() : failed to write fees to cert DB");

					// debug
					printf( "CONNECTED CERT: op=%s cert=%s title=%s qty=%d hash=%s height=%d fees=%llu\n",
							certFromOp(op).c_str(),
							stringFromVch(vvchArgs[0]).c_str(),
							stringFromVch(theCert.sTitle).c_str(),
							theCert.GetRemQty(),
							tx.GetHash().ToString().c_str(), 
							nHeight, nTheFee / COIN);
				}
			}

			if (pindexBlock->nHeight != pindexBest->nHeight) {
				// activate or update - seller txn
				if (op == OP_CERTISSUER_NEW || op == OP_CERTISSUER_ACTIVATE || op == OP_CERTISSUER_UPDATE) {
					vector<unsigned char> vchCertIssuer = op == OP_CERTISSUER_NEW ? vchFromString(HexStr(vvchArgs[0])) : vvchArgs[0];
					LOCK(cs_main);
					std::map<std::vector<unsigned char>, std::set<uint256> >::iterator mi = mapCertPending.find(vchCertIssuer);
					if (mi != mapCertPending.end())
						mi->second.erase(tx.GetHash());
				}

				// accept or pay - buyer txn
				else if (op == OP_CERT_NEW || op == OP_CERT_TRANSFER) {
					LOCK(cs_main);
					std::map<std::vector<unsigned char>, std::set<uint256> >::iterator mi = mapCertPending.find(vvchArgs[1]);
					if (mi != mapCertPending.end())
						mi->second.erase(tx.GetHash());
				}
			}
		}
	}
	return true;
}

bool ExtractCertAddress(const CScript& script, string& address) {
	if (script.size() == 1 && script[0] == OP_RETURN) {
		address = string("network fee");
		return true;
	}
	vector<vector<unsigned char> > vvch;
	int op;
	if (!DecodeCertScript(script, op, vvch))
		return false;

	string strOp = certFromOp(op);
	string strCert;
	if (op == OP_CERTISSUER_NEW) {
#ifdef GUI
		LOCK(cs_main);

		std::map<uint160, std::vector<unsigned char> >::const_iterator mi = mapMyCertHashes.find(uint160(vvch[0]));
		if (mi != mapMyCertHashes.end())
		strCert = stringFromVch(mi->second);
		else
#endif
		strCert = HexStr(vvch[0]);
	} 
	else
		strCert = stringFromVch(vvch[0]);

	address = strOp + ": " + strCert;
	return true;
}

void rescanforcerts(CBlockIndex *pindexRescan) {
    printf("Scanning blockchain for certs to create fast index...\n");
    pcertdb->ReconstructCertIndex(pindexRescan);
}

int GetCertTxPosHeight(const CDiskTxPos& txPos) {
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

int GetCertTxPosHeight2(const CDiskTxPos& txPos, int nHeight) {
    nHeight = GetCertTxPosHeight(txPos);
    return nHeight;
}

Value certissuernew(const Array& params, bool fHelp) {
	if (fHelp || params.size() < 5 || params.size() > 6)
		throw runtime_error(
				"certissuernew [<address>] <category> <title> <quantity> <price> [<description>]\n"
						"<category> category, 255 chars max."
						+ HelpRequiringPassphrase());
	// gather inputs
	string baSig;
	unsigned int nParamIdx = 0;
	int64 nQty, nPrice;
	
	vector<unsigned char> vchPaymentAddress = vchFromValue(params[nParamIdx]);
	CBitcoinAddress payAddr(stringFromVch(vchPaymentAddress));
	if(payAddr.IsValid()) nParamIdx++;
	else {
	    BOOST_FOREACH(const PAIRTYPE(CTxDestination, string)& entry, pwalletMain->mapAddressBook) {
	        if (IsMine(*pwalletMain, entry.first)) {
	            // sign the data and store it as the alias value
	            CKeyID keyID;
	            payAddr.Set(entry.first);
	            if (payAddr.GetKeyID(keyID) && payAddr.IsValid()) {
		            vchPaymentAddress = vchFromString(payAddr.ToString());
		            break;
	            }
	        }
	    }
	}

	vector<unsigned char> vchCat = vchFromValue(params[nParamIdx++]);
	vector<unsigned char> vchTitle = vchFromValue(params[nParamIdx++]);
	vector<unsigned char> vchDesc;

	nQty = atoi(params[nParamIdx++].get_str().c_str());
	nPrice = atoi64(params[nParamIdx++].get_str().c_str());

	if(nParamIdx < params.size()) vchDesc = vchFromValue(params[nParamIdx++]);
    else vchDesc = vchFromString("");

    // 64Kbyte cert desc. maxlen
	if (vchDesc.size() > 1024 * 64)
		throw JSONRPCError(RPC_INVALID_PARAMETERa, "Cert description is too long.");

	// set wallet tx ver
	CWalletTx wtx;
	wtx.nVersion = SYSCOIN_TX_VERSION;

	// generate rand identifier
	uint64 rand = GetRand((uint64) -1);
	vector<unsigned char> vchRand = CBigNum(rand).getvch();
	vector<unsigned char> vchCertIssuer = vchFromString(HexStr(vchRand));
	vector<unsigned char> vchToHash(vchRand);
	vchToHash.insert(vchToHash.end(), vchCertIssuer.begin(), vchCertIssuer.end());
	uint160 certHash = Hash160(vchToHash);

	// build cert object
	CCertIssuer newCert;
	newCert.vchRand = vchRand;
	newCert.vchPaymentAddress = vchPaymentAddress;
	newCert.sCategory = vchCat;
	newCert.sTitle = vchTitle;
	newCert.sDescription = vchDesc;
	newCert.nQty = nQty;
	newCert.nPrice = nPrice;

	string bdata = newCert.SerializeToString();

	// create transaction keys
	CPubKey newDefaultKey;
	pwalletMain->GetKeyFromPool(newDefaultKey, false);
	CScript scriptPubKeyOrig;
	scriptPubKeyOrig.SetDestination(newDefaultKey.GetID());
	CScript scriptPubKey;
	scriptPubKey << CScript::EncodeOP_N(OP_CERTISSUER_NEW) << certHash << OP_2DROP;
	scriptPubKey += scriptPubKeyOrig;

	// send transaction
	{
		LOCK(cs_main);
		EnsureWalletIsUnlocked();
		string strError = pwalletMain->SendMoney(scriptPubKey, MIN_AMOUNT, wtx,
				false, bdata);
		if (strError != "")
			throw JSONRPCError(RPC_WALLET_ERROR, strError);
		mapMyCerts[vchCertIssuer] = wtx.GetHash();
	}
	printf("SENT:CERTNEW : title=%s, rand=%s, tx=%s, data:\n%s\n",
			stringFromVch(vchTitle).c_str(), stringFromVch(vchCertIssuer).c_str(),
			wtx.GetHash().GetHex().c_str(), bdata.c_str());

	// return results
	vector<Value> res;
	res.push_back(wtx.GetHash().GetHex());
	res.push_back(HexStr(vchRand));

	return res;
}

Value certactivate(const Array& params, bool fHelp) {
	if (fHelp || params.size() < 1 || params.size() > 2)
		throw runtime_error(
				"certactivate <rand> [<tx>]\n"
						"Activate an cert after creating it with certissuernew.\n"
						+ HelpRequiringPassphrase());

	// gather inputs
	vector<unsigned char> vchRand = ParseHex(params[0].get_str());
	vector<unsigned char> vchCertIssuer = vchFromValue(params[0]);

	// this is a syscoin transaction
	CWalletTx wtx;
	wtx.nVersion = SYSCOIN_TX_VERSION;

	// check for existing pending certs
	{
		LOCK2(cs_main, pwalletMain->cs_wallet);
		if (mapCertPending.count(vchCertIssuer)
				&& mapCertPending[vchCertIssuer].size()) {
			error( "certactivate() : there are %d pending operations on that cert, including %s",
				   (int) mapCertPending[vchCertIssuer].size(),
				   mapCertPending[vchCertIssuer].begin()->GetHex().c_str());
			throw runtime_error("there are pending operations on that cert");
		}

		// look for an cert with identical hex rand keys. wont happen.
		CTransaction tx;
		if (GetTxOfCert(*pcertdb, vchCertIssuer, tx)) {
			error( "certactivate() : this cert is already active with tx %s",
				   tx.GetHash().GetHex().c_str());
			throw runtime_error("this cert is already active");
		}

		EnsureWalletIsUnlocked();

		// Make sure there is a previous certissuernew tx on this cert and that the random value matches
		uint256 wtxInHash;
		if (params.size() == 1) {
			if (!mapMyCerts.count(vchCertIssuer))
				throw runtime_error(
						"could not find a coin with this cert, try specifying the certissuernew transaction id");
			wtxInHash = mapMyCerts[vchCertIssuer];
		} else
			wtxInHash.SetHex(params[1].get_str());
		if (!pwalletMain->mapWallet.count(wtxInHash))
			throw runtime_error("previous transaction is not in the wallet");

		// verify previous txn was certissuernew
		CWalletTx& wtxIn = pwalletMain->mapWallet[wtxInHash];
		vector<unsigned char> vchHash;
		bool found = false;
		BOOST_FOREACH(CTxOut& out, wtxIn.vout) {
			vector<vector<unsigned char> > vvch;
			int op;
			if (DecodeCertScript(out.scriptPubKey, op, vvch)) {
				if (op != OP_CERTISSUER_NEW)
					throw runtime_error(
							"previous transaction wasn't a certissuernew");
				vchHash = vvch[0]; found = true;
				break;
			}
		}
		if (!found)
			throw runtime_error("Could not decode cert transaction");

		// calculate network fees
		int64 nNetFee = GetCertNetworkFee(8, pindexBest->nHeight);

		// unserialize cert object from txn, serialize back
		CCertIssuer newCert;
		if(!newCert.UnserializeFromTx(wtxIn))
			throw runtime_error(
					"could not unserialize cert from txn");

		newCert.nFee = nNetFee;

		string bdata = newCert.SerializeToString();
		vector<unsigned char> vchbdata = vchFromString(bdata);

		// check this hash against previous, ensure they match
		vector<unsigned char> vchToHash(vchRand);
		vchToHash.insert(vchToHash.end(), vchCertIssuer.begin(), vchCertIssuer.end());
		uint160 hash = Hash160(vchToHash);
		if (uint160(vchHash) != hash)
			throw runtime_error("previous tx used a different random value");

		//create certactivate txn keys
		CPubKey newDefaultKey;
		pwalletMain->GetKeyFromPool(newDefaultKey, false);
		CScript scriptPubKeyOrig;
		scriptPubKeyOrig.SetDestination(newDefaultKey.GetID());
		CScript scriptPubKey;
		scriptPubKey << CScript::EncodeOP_N(OP_CERTISSUER_ACTIVATE) << vchCertIssuer
				<< vchRand << newCert.sTitle << OP_2DROP << OP_2DROP;
		scriptPubKey += scriptPubKeyOrig;

		// send the tranasction
		string strError = SendCertMoneyWithInputTx(scriptPubKey, MIN_AMOUNT,
				nNetFee, wtxIn, wtx, false, bdata);
		if (strError != "")
			throw JSONRPCError(RPC_WALLET_ERROR, strError);

		printf("SENT:CERTACTIVATE: title=%s, rand=%s, tx=%s, data:\n%s\n",
				stringFromVch(newCert.sTitle).c_str(),
				stringFromVch(vchCertIssuer).c_str(), wtx.GetHash().GetHex().c_str(),
				stringFromVch(vchbdata).c_str() );
	}
	return wtx.GetHash().GetHex();
}

Value certupdate(const Array& params, bool fHelp) {
	if (fHelp || params.size() < 5 || params.size() > 6)
		throw runtime_error(
				"certupdate <rand> <category> <title> <quantity> <price> [<description>]\n"
						"Perform an update on an cert you control.\n"
						+ HelpRequiringPassphrase());

	// gather & validate inputs
	vector<unsigned char> vchRand = ParseHex(params[0].get_str());
	vector<unsigned char> vchCertIssuer = vchFromValue(params[0]);
	vector<unsigned char> vchCat = vchFromValue(params[1]);
	vector<unsigned char> vchTitle = vchFromValue(params[2]);
	vector<unsigned char> vchDesc;
	int qty;
	uint64 price;
	if (params.size() == 6) vchDesc = vchFromValue(params[5]);
	try {
		qty = atoi(params[3].get_str().c_str());
		price = atoi(params[4].get_str().c_str());
	} catch (std::exception &e) {
		throw runtime_error("invalid price and/or quantity values.");
	}
	if (vchDesc.size() > 1024 * 1024)
		throw JSONRPCError(RPC_INVALID_PARAMETER, "Description is too long.");

	// this is a syscoind txn
	CWalletTx wtx;
	wtx.nVersion = SYSCOIN_TX_VERSION;
	CScript scriptPubKeyOrig;

	// get a key from our wallet set dest as ourselves
	CPubKey newDefaultKey;
	pwalletMain->GetKeyFromPool(newDefaultKey, false);
	scriptPubKeyOrig.SetDestination(newDefaultKey.GetID());

	// create CERTUPDATE txn keys
	CScript scriptPubKey;
	scriptPubKey << CScript::EncodeOP_N(OP_CERTISSUER_UPDATE) << vchCertIssuer << vchTitle
			<< OP_2DROP << OP_DROP;
	scriptPubKey += scriptPubKeyOrig;

	{
		LOCK2(cs_main, pwalletMain->cs_wallet);

		if (mapCertPending.count(vchCertIssuer)
				&& mapCertPending[vchCertIssuer].size()) 
			throw runtime_error("there are pending operations on that cert");

		EnsureWalletIsUnlocked();

		// look for a transaction with this key
		CTransaction tx;
		if (!GetTxOfCert(*pcertdb, vchCertIssuer, tx))
			throw runtime_error("could not find an cert with this name");

		// make sure cert is in wallet
		uint256 wtxInHash = tx.GetHash();
		if (!pwalletMain->mapWallet.count(wtxInHash)) 
			throw runtime_error("this cert is not in your wallet");
		
		// unserialize cert object from txn
		CCertIssuer theCert;
		if(!theCert.UnserializeFromTx(tx))
			throw runtime_error("cannot unserialize cert from txn");

		// get the cert from DB
		vector<CCertIssuer> vtxPos;
		if (!pcertdb->ReadCert(vchCertIssuer, vtxPos))
			throw runtime_error("could not read cert from DB");
		theCert = vtxPos.back();

		// calculate network fees
		int64 nNetFee = GetCertNetworkFee(4, pindexBest->nHeight);
		if(qty > 0) nNetFee += (price * qty) / 200;

		// update cert values
		theCert.sCategory = vchCat;
		theCert.sTitle = vchTitle;
		theCert.sDescription = vchDesc;
		if(theCert.GetRemQty() + qty >= 0)
			theCert.nQty += qty;
		theCert.nPrice = price;
		theCert.nFee += nNetFee;

		// serialize cert object
		string bdata = theCert.SerializeToString();

		CWalletTx& wtxIn = pwalletMain->mapWallet[wtxInHash];
		string strError = SendCertMoneyWithInputTx(scriptPubKey, MIN_AMOUNT, nNetFee,
				wtxIn, wtx, false, bdata);
		if (strError != "")
			throw JSONRPCError(RPC_WALLET_ERROR, strError);
	}
	return wtx.GetHash().GetHex();
}

Value certaccept(const Array& params, bool fHelp) {
	if (fHelp || params.size() < 1 || params.size() > 2)
		throw runtime_error("certaccept <rand> [<quantity]>\n"
				"Accept an cert.\n" + HelpRequiringPassphrase());

	vector<unsigned char> vchCertIssuer = vchFromValue(params[0]);
	vector<unsigned char> vchQty;
	int nQty=1;
	if (params.size() == 2) {
		try {
			nQty=atoi(params[1].get_str().c_str());
		} catch (std::exception &e) {
			throw runtime_error("invalid price and/or quantity values.");
		}
		vchQty = vchFromValue(params[1]);
	} else vchQty = vchFromValue("1");

	// this is a syscoin txn
	CWalletTx wtx;
	wtx.nVersion = SYSCOIN_TX_VERSION;
	CScript scriptPubKeyOrig;

	// generate cert accept identifier and hash
	uint64 rand = GetRand((uint64) -1);
	vector<unsigned char> vchAcceptRand = CBigNum(rand).getvch();
	vector<unsigned char> vchAccept = vchFromString(HexStr(vchAcceptRand));
	vector<unsigned char> vchToHash(vchAcceptRand);
	vchToHash.insert(vchToHash.end(), vchCertIssuer.begin(), vchCertIssuer.end());
	uint160 acceptHash = Hash160(vchToHash);

	// get a key from our wallet set dest as ourselves
	CPubKey newDefaultKey;
	pwalletMain->GetKeyFromPool(newDefaultKey, false);
	scriptPubKeyOrig.SetDestination(newDefaultKey.GetID());

	// create CERTACCEPT txn keys
	CScript scriptPubKey;
	scriptPubKey << CScript::EncodeOP_N(OP_CERT_NEW)
			<< vchCertIssuer << vchAcceptRand << acceptHash << OP_2DROP << OP_2DROP;
	scriptPubKey += scriptPubKeyOrig;
	{
		LOCK2(cs_main, pwalletMain->cs_wallet);

		if (mapCertPending.count(vchCertIssuer)
				&& mapCertPending[vchCertIssuer].size()) {
			error(  "certaccept() : there are %d pending operations on that cert, including %s",
					(int) mapCertPending[vchCertIssuer].size(),
					mapCertPending[vchCertIssuer].begin()->GetHex().c_str());
			throw runtime_error("there are pending operations on that cert");
		}

		EnsureWalletIsUnlocked();

		// look for a transaction with this key
		CTransaction tx;
		if (!GetTxOfCert(*pcertdb, vchCertIssuer, tx))
			throw runtime_error("could not find an cert with this identifier");

		// unserialize cert object from txn
		CCertIssuer theCert;
		if(!theCert.UnserializeFromTx(tx))
			throw runtime_error("could not unserialize cert from txn");

		// get the cert id from DB
		vector<CCertIssuer> vtxPos;
		if (!pcertdb->ReadCert(vchCertIssuer, vtxPos))
			throw runtime_error("could not read cert with this name from DB");
		theCert = vtxPos.back();

		if(theCert.GetRemQty() < nQty)
			throw runtime_error("not enough remaining quantity to fulfill this orderaccept");

		// create accept object
		CCert txAccept;
		txAccept.vchRand = vchAcceptRand;
		txAccept.nQty = nQty;
		txAccept.nPrice = theCert.nPrice;
		theCert.accepts.clear();
		theCert.PutCert(txAccept);

		// serialize cert object
		string bdata = theCert.SerializeToString();

		string strError = pwalletMain->SendMoney(scriptPubKey, MIN_AMOUNT, wtx,
				false, bdata);
		if (strError != "")
			throw JSONRPCError(RPC_WALLET_ERROR, strError);
		mapMyCerts[vchAcceptRand] = wtx.GetHash();
	}
	// return results
	vector<Value> res;
	res.push_back(wtx.GetHash().GetHex());
	res.push_back(HexStr(vchAcceptRand));

	return res;
}

Value certpay(const Array& params, bool fHelp) {
	if (fHelp || 2 != params.size())
		throw runtime_error("certpay <rand> <message>\n"
				"Pay for a confirmed accepted cert.\n"
				+ HelpRequiringPassphrase());

	// gather & validate inputs
	vector<unsigned char> vchRand = ParseHex(params[0].get_str());
	vector<unsigned char> vchMessage = vchFromValue(params[1]);

	// this is a syscoin txn
	CWalletTx wtx, wtxPay;
	wtx.nVersion = SYSCOIN_TX_VERSION;
	CScript scriptPubKeyOrig;

	{
	LOCK2(cs_main, pwalletMain->cs_wallet);

	if (mapCertPending.count(vchRand)
			&& mapCertPending[vchRand].size()) 
		throw runtime_error( "certpay() : there are pending operations on that cert" );

	EnsureWalletIsUnlocked();

	// look for a transaction with this key, also returns
	// an cert object if it is found
	CTransaction tx;
	CCertIssuer theCert;
	CCert theCert;
	if (!GetTxOfCert(*pcertdb, vchRand, theCert, tx))
		throw runtime_error("could not find an cert with this name");

	// check to see if cert accept in wallet
	uint256 wtxInHash = tx.GetHash();
	if (!pwalletMain->mapWallet.count(wtxInHash)) 
		throw runtime_error("certpay() : cert accept is not in your wallet" );

	// check that prev txn contains cert
	if(!theCert.UnserializeFromTx(tx))
		throw runtime_error("could not unserialize cert from txn");

	// get the cert accept from cert
	if(!theCert.GetAcceptByHash(vchRand, theCert))
		throw runtime_error("could not find an cert accept with this name");

	// get the cert id from DB
	vector<unsigned char> vchCertIssuer;
	vector<CCertIssuer> vtxPos;
	if (!pcertdb->ReadCert(vchRand, vchCertIssuer))
		throw runtime_error("could not read cert of accept from DB");
	if (!pcertdb->ReadCert(vchCertIssuer, vtxPos))
		throw runtime_error("could not read cert with this name from DB");

	// hashes should match
	if(vtxPos.back().vchRand != theCert.vchRand)
		throw runtime_error("cert hash mismatch.");

	// use the cert and accept from the DB as basis
    theCert = vtxPos.back();
    if(!theCert.GetAcceptByHash(vchRand, theCert))
		throw runtime_error("could not find an cert accept with this hash in DB");

	// check if paid already
	if(theCert.bPaid)
		throw runtime_error("This cert accept has already been successfully paid.");

	// get a key from our wallet set dest as ourselves
	CPubKey newDefaultKey;
	pwalletMain->GetKeyFromPool(newDefaultKey, false);
	scriptPubKeyOrig.SetDestination(newDefaultKey.GetID());

	// create CERTPAY txn keys
	CScript scriptPubKey;
	scriptPubKey << CScript::EncodeOP_N(OP_CERT_TRANSFER) << vchCertIssuer << vchRand << OP_2DROP << OP_DROP;
	scriptPubKey += scriptPubKeyOrig;

    // make sure wallet is unlocked
    if (pwalletMain->IsLocked()) throw JSONRPCError(RPC_WALLET_UNLOCK_NEEDED, 
    	"Error: Please enter the wallet passphrase with walletpassphrase first.");

    // Check for sufficient funds to pay for order
    set<pair<const CWalletTx*,unsigned int> > setCoins;
    int64 nValueIn = 0;
    uint64 nTotalValue = ( theCert.nPrice * theCert.nQty );
    int64 nNetFee = GetCertNetworkFee(4, pindexBest->nHeight) + (nTotalValue / 200);
    if (!pwalletMain->SelectCoins(nTotalValue + nNetFee, setCoins, nValueIn)) 
        throw runtime_error("insufficient funds to pay for cert");

    theCert.vchRand = vchRand;
    theCert.txPayId = wtxPay.GetHash();
    theCert.vchMessage = vchMessage;
    theCert.nFee = nNetFee;
    theCert.accepts.clear();
    theCert.PutCert(theCert);
    printf("certaccept msg %s\n", stringFromVch(theCert.vchMessage).c_str());

    // add a copy of the cert object with just
    // the one accept object to payment txn to identify
    // this txn as an cert payment
    CCertIssuer certCopy = theCert;
    CCert certAcceptCopy = theCert;
    certAcceptCopy.bPaid = true;
    certCopy.accepts.clear();
    certCopy.PutCert(certAcceptCopy);

    // send payment to cert address
    CBitcoinAddress address(stringFromVch(theCert.vchPaymentAddress));
    string strError = pwalletMain->SendMoneyToDestination(address.Get(), nTotalValue, 
    	wtxPay, false, certCopy.SerializeToString());
    if (strError != "") throw JSONRPCError(RPC_WALLET_ERROR, strError);

	// send the cert pay txn 
	CWalletTx& wtxIn = pwalletMain->mapWallet[wtxInHash];
	strError = SendCertMoneyWithInputTx(scriptPubKey, MIN_AMOUNT, nNetFee,
			wtxIn, wtx, false, theCert.SerializeToString());
	if (strError != "")
		throw JSONRPCError(RPC_WALLET_ERROR, strError);
	}
	mapMyCerts[vchRand] = wtx.GetHash();
	
	// return results
	vector<Value> res;
	res.push_back(wtxPay.GetHash().GetHex());
	res.push_back(wtx.GetHash().GetHex());

	return res;
}

Value certinfo(const Array& params, bool fHelp) {
	if (fHelp || 1 != params.size())
		throw runtime_error("certinfo <rand>\n"
				"Show values of an cert.\n");

	Object oLastCert;
	vector<unsigned char> vchCertIssuer = vchFromValue(params[0]);
	string cert = stringFromVch(vchCertIssuer);
	{
		LOCK(pwalletMain->cs_wallet);
		vector<CCertIssuer> vtxPos;
		if (!pcertdb->ReadCert(vchCertIssuer, vtxPos))
			throw JSONRPCError(RPC_WALLET_ERROR,
					"failed to read from cert DB");

		if (vtxPos.size() < 1)
			throw JSONRPCError(RPC_WALLET_ERROR, "no result returned");

		uint256 blockHash;
		uint256 txHash = vtxPos[vtxPos.size() - 1].txHash;
		CTransaction tx;
		if (!GetTransaction(txHash, tx, blockHash, true))
			throw JSONRPCError(RPC_WALLET_ERROR,
					"failed to read transaction from disk");

		CCertIssuer theCert;
		if(!theCert.UnserializeFromTx(tx))
			throw JSONRPCError(RPC_WALLET_ERROR,
					"failed to unserialize cert from transaction");

		theCert = vtxPos.back();

		Object oCert;
		vector<unsigned char> vchValue;
		Array aoCerts;
		for(unsigned int i=0;i<theCert.accepts.size();i++) {
			CCert ca = theCert.accepts[i];
			Object oCert;
			oCert.push_back(Pair("id", HexStr(ca.vchRand)));
			oCert.push_back(Pair("txid", ca.txHash.GetHex()));
			oCert.push_back(Pair("height", (double)ca.nHeight));
			oCert.push_back(Pair("time", ca.nTime));
			oCert.push_back(Pair("quantity", ca.nQty));
			oCert.push_back(Pair("price", (double)ca.nPrice / COIN));
			oCert.push_back(Pair("paid", ca.bPaid?"true":"false"));
			if(ca.bPaid) {
				oCert.push_back(Pair("fee", (double)ca.nFee / COIN));
				oCert.push_back(Pair("paytxid", ca.txPayId.GetHex()));
				oCert.push_back(Pair("message", stringFromVch(ca.vchMessage)));
			}
			aoCerts.push_back(oCert);
		}
		int nHeight;
		uint256 certHash;
		if (GetValueOfCertTxHash(txHash, vchValue, certHash, nHeight)) {
			oCert.push_back(Pair("id", cert));
			oCert.push_back(Pair("txid", tx.GetHash().GetHex()));
			string strAddress = "";
			GetCertIssuerAddress(tx, strAddress);
			oCert.push_back(Pair("address", strAddress));
			oCert.push_back(
					Pair("expires_in",
							nHeight + GetCertIssuerDisplayExpirationDepth(nHeight)
									- pindexBest->nHeight));
			if (nHeight + GetCertIssuerDisplayExpirationDepth(nHeight)
					- pindexBest->nHeight <= 0) {
				oCert.push_back(Pair("expired", 1));
			}
			oCert.push_back(Pair("payment_address", stringFromVch(theCert.vchPaymentAddress)));
			oCert.push_back(Pair("category", stringFromVch(theCert.sCategory)));
			oCert.push_back(Pair("title", stringFromVch(theCert.sTitle)));
			oCert.push_back(Pair("quantity", theCert.GetRemQty()));
			oCert.push_back(Pair("price", (double)theCert.nPrice / COIN));
			oCert.push_back(Pair("fee", (double)theCert.nFee / COIN));
			oCert.push_back(Pair("description", stringFromVch(theCert.sDescription)));
			oCert.push_back(Pair("accepts", aoCerts));
			oLastCert = oCert;
		}
	}
	return oLastCert;

}

Value certlist(const Array& params, bool fHelp) {
	if (fHelp || 1 != params.size())
		throw runtime_error("certlist [<cert>]\n"
				"list my own certs");

	vector<unsigned char> vchCertIssuer;
	vector<unsigned char> vchLastCert;

	if (params.size() == 1)
		vchCertIssuer = vchFromValue(params[0]);

	vector<unsigned char> vchCertUniq;
	if (params.size() == 1)
		vchCertUniq = vchFromValue(params[0]);

	Array oRes;
	map<vector<unsigned char>, int> vCertsI;
	map<vector<unsigned char>, Object> vCertsO;

	{
		LOCK(pwalletMain->cs_wallet);

		CDiskTxPos txindex;
		uint256 hash;
		CTransaction tx;

		vector<unsigned char> vchValue;
		int nHeight;

		BOOST_FOREACH(PAIRTYPE(const uint256, CWalletTx)& item, pwalletMain->mapWallet) {
			hash = item.second.GetHash();
			if (!pblocktree->ReadTxIndex(hash, txindex))
				continue;

			if (tx.nVersion != SYSCOIN_TX_VERSION)
				continue;

			// cert
			if (!GetNameOfCertTx(tx, vchCertIssuer))
				continue;
			if (vchCertUniq.size() > 0 && vchCertUniq != vchCertIssuer)
				continue;

			// value
			if (!GetValueOfCertTx(tx, vchValue))
				continue;

			// height
			nHeight = GetCertTxPosHeight(txindex);

			Object oCert;
			oCert.push_back(Pair("cert", stringFromVch(vchCertIssuer)));
			oCert.push_back(Pair("value", stringFromVch(vchValue)));
			if (!IsCertTxnMine(pwalletMain->mapWallet[tx.GetHash()]))
				oCert.push_back(Pair("transferred", 1));
			string strAddress = "";
			GetCertIssuerAddress(tx, strAddress);
			oCert.push_back(Pair("address", strAddress));
			oCert.push_back(
					Pair("expires_in",
							nHeight + GetCertIssuerDisplayExpirationDepth(nHeight)
									- pindexBest->nHeight));
			if (nHeight + GetCertIssuerDisplayExpirationDepth(nHeight)
					- pindexBest->nHeight <= 0) {
				oCert.push_back(Pair("expired", 1));
			}

			// get last active cert only
			if (vCertsI.find(vchCertIssuer) != vCertsI.end()
					&& vCertsI[vchCertIssuer] > nHeight)
				continue;

			vCertsI[vchCertIssuer] = nHeight;
			vCertsO[vchCertIssuer] = oCert;
		}

	}

	BOOST_FOREACH(const PAIRTYPE(vector<unsigned char>, Object)& item, vCertsO)
		oRes.push_back(item.second);

	return oRes;

	return (double) 0;
}

Value certhistory(const Array& params, bool fHelp) {
	if (fHelp || 1 != params.size())
		throw runtime_error("certhistory <cert>\n"
				"List all stored values of an cert.\n");

	Array oRes;
	vector<unsigned char> vchCertIssuer = vchFromValue(params[0]);
	string cert = stringFromVch(vchCertIssuer);

	{
		LOCK(pwalletMain->cs_wallet);

		//vector<CDiskTxPos> vtxPos;
		vector<CCertIssuer> vtxPos;
		//CCertDB dbCert("r");
		if (!pcertdb->ReadCert(vchCertIssuer, vtxPos))
			throw JSONRPCError(RPC_WALLET_ERROR,
					"failed to read from cert DB");

		CCertIssuer txPos2;
		uint256 txHash;
		uint256 blockHash;
		BOOST_FOREACH(txPos2, vtxPos) {
			txHash = txPos2.txHash;
			CTransaction tx;
			if (!GetTransaction(txHash, tx, blockHash, true)) {
				error("could not read txpos");
				continue;
			}

			Object oCert;
			vector<unsigned char> vchValue;
			int nHeight;
			uint256 hash;
			if (GetValueOfCertTxHash(txHash, vchValue, hash, nHeight)) {
				oCert.push_back(Pair("cert", cert));
				string value = stringFromVch(vchValue);
				oCert.push_back(Pair("value", value));
				oCert.push_back(Pair("txid", tx.GetHash().GetHex()));
				string strAddress = "";
				GetCertIssuerAddress(tx, strAddress);
				oCert.push_back(Pair("address", strAddress));
				oCert.push_back(
						Pair("expires_in",
								nHeight + GetCertIssuerDisplayExpirationDepth(nHeight)
										- pindexBest->nHeight));
				if (nHeight + GetCertIssuerDisplayExpirationDepth(nHeight)
						- pindexBest->nHeight <= 0) {
					oCert.push_back(Pair("expired", 1));
				}
				oRes.push_back(oCert);
			}
		}
	}
	return oRes;
}

Value certfilter(const Array& params, bool fHelp) {
	if (fHelp || params.size() > 5)
		throw runtime_error(
				"certfilter [[[[[regexp] maxage=36000] from=0] nb=0] stat]\n"
						"scan and filter certes\n"
						"[regexp] : apply [regexp] on certes, empty means all certes\n"
						"[maxage] : look in last [maxage] blocks\n"
						"[from] : show results from number [from]\n"
						"[nb] : show [nb] results, 0 means all\n"
						"[stats] : show some stats instead of results\n"
						"certfilter \"\" 5 # list certes updated in last 5 blocks\n"
						"certfilter \"^cert\" # list all certes starting with \"cert\"\n"
						"certfilter 36000 0 0 stat # display stats (number of certs) on active certes\n");

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

	//CCertDB dbCert("r");
	Array oRes;

	vector<unsigned char> vchCertIssuer;
	vector<pair<vector<unsigned char>, CCertIssuer> > certScan;
	if (!pcertdb->ScanCerts(vchCertIssuer, 100000000, certScan))
		throw JSONRPCError(RPC_WALLET_ERROR, "scan failed");

	pair<vector<unsigned char>, CCertIssuer> pairScan;
	BOOST_FOREACH(pairScan, certScan) {
		string cert = stringFromVch(pairScan.first);

		// regexp
		using namespace boost::xpressive;
		smatch certparts;
		sregex cregex = sregex::compile(strRegexp);
		if (strRegexp != "" && !regex_search(cert, certparts, cregex))
			continue;

		CCertIssuer txCertIssuer = pairScan.second;
		int nHeight = txCertIssuer.nHeight;

		// max age
		if (nMaxAge != 0 && pindexBest->nHeight - nHeight >= nMaxAge)
			continue;

		// from limits
		nCountFrom++;
		if (nCountFrom < nFrom + 1)
			continue;

		Object oCert;
		oCert.push_back(Pair("cert", cert));
		CTransaction tx;
		uint256 blockHash;
		uint256 txHash = txCertIssuer.txHash;
		if ((nHeight + GetCertIssuerDisplayExpirationDepth(nHeight) - pindexBest->nHeight
				<= 0) || !GetTransaction(txHash, tx, blockHash, true)) {
			oCert.push_back(Pair("expired", 1));
		} else {
			vector<unsigned char> vchValue = txCertIssuer.sTitle;
			string value = stringFromVch(vchValue);
			oCert.push_back(Pair("value", value));
			oCert.push_back(
					Pair("expires_in",
							nHeight + GetCertIssuerDisplayExpirationDepth(nHeight)
									- pindexBest->nHeight));
		}
		oRes.push_back(oCert);

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

Value certscan(const Array& params, bool fHelp) {
	if (fHelp || 2 > params.size())
		throw runtime_error(
				"certscan [<start-cert>] [<max-returned>]\n"
						"scan all certs, starting at start-cert and returning a maximum number of entries (default 500)\n");

	vector<unsigned char> vchCertIssuer;
	int nMax = 500;
	if (params.size() > 0) {
		vchCertIssuer = vchFromValue(params[0]);
	}

	if (params.size() > 1) {
		Value vMax = params[1];
		ConvertTo<double>(vMax);
		nMax = (int) vMax.get_real();
	}

	//CCertDB dbCert("r");
	Array oRes;

	vector<pair<vector<unsigned char>, CCertIssuer> > certScan;
	if (!pcertdb->ScanCerts(vchCertIssuer, nMax, certScan))
		throw JSONRPCError(RPC_WALLET_ERROR, "scan failed");

	pair<vector<unsigned char>, CCertIssuer> pairScan;
	BOOST_FOREACH(pairScan, certScan) {
		Object oCert;
		string cert = stringFromVch(pairScan.first);
		oCert.push_back(Pair("cert", cert));
		CTransaction tx;
		CCertIssuer txCertIssuer = pairScan.second;
		uint256 blockHash;

		int nHeight = txCertIssuer.nHeight;
		vector<unsigned char> vchValue = txCertIssuer.sTitle;
		if ((nHeight + GetCertIssuerDisplayExpirationDepth(nHeight) - pindexBest->nHeight
				<= 0) || !GetTransaction(txCertIssuer.txHash, tx, blockHash, true)) {
			oCert.push_back(Pair("expired", 1));
		} else {
			string value = stringFromVch(vchValue);
			//string strAddress = "";
			//GetCertIssuerAddress(tx, strAddress);
			oCert.push_back(Pair("value", value));
			//oCert.push_back(Pair("txid", tx.GetHash().GetHex()));
			//oCert.push_back(Pair("address", strAddress));
			oCert.push_back(
					Pair("expires_in",
							nHeight + GetCertIssuerDisplayExpirationDepth(nHeight)
									- pindexBest->nHeight));
		}
		oRes.push_back(oCert);
	}

	return oRes;
}



/*
 Value certclean(const Array& params, bool fHelp)
 {
 if (fHelp || params.size())
 throw runtime_error("cert_clean\nClean unsatisfiable transactions from the wallet - including cert_update on an already taken cert\n");


 {
 LOCK2(cs_main,pwalletMain->cs_wallet);
 map<uint256, CWalletTx> mapRemove;

 printf("-----------------------------\n");

 {
 BOOST_FOREACH(PAIRTYPE(const uint256, CWalletTx)& item, pwalletMain->mapWallet)
 {
 CWalletTx& wtx = item.second;
 vector<unsigned char> vchCertIssuer;
 if (wtx.GetDepthInMainChain() < 1 && IsConflictedAliasTx(pblocktree, wtx, vchCertIssuer))
 {
 uint256 hash = wtx.GetHash();
 mapRemove[hash] = wtx;
 }
 }
 }

 bool fRepeat = true;
 while (fRepeat)
 {
 fRepeat = false;
 BOOST_FOREACH(PAIRTYPE(const uint256, CWalletTx)& item, pwalletMain->mapWallet)
 {
 CWalletTx& wtx = item.second;
 BOOST_FOREACH(const CTxIn& txin, wtx.vin)
 {
 uint256 hash = wtx.GetHash();

 // If this tx depends on a tx to be removed, remove it too
 if (mapRemove.count(txin.prevout.hash) && !mapRemove.count(hash))
 {
 mapRemove[hash] = wtx;
 fRepeat = true;
 }
 }
 }
 }

 BOOST_FOREACH(PAIRTYPE(const uint256, CWalletTx)& item, mapRemove)
 {
 CWalletTx& wtx = item.second;

 UnspendInputs(wtx);
 wtx.RemoveFromMemoryPool();
 pwalletMain->EraseFromWallet(wtx.GetHash());
 vector<unsigned char> vchCertIssuer;
 if (GetNameOfCertTx(wtx, vchCertIssuer) && mapCertPending.count(vchCertIssuer))
 {
 string cert = stringFromVch(vchCertIssuer);
 printf("cert_clean() : erase %s from pending of cert %s",
 wtx.GetHash().GetHex().c_str(), cert.c_str());
 if (!mapCertPending[vchCertIssuer].erase(wtx.GetHash()))
 error("cert_clean() : erase but it was not pending");
 }
 wtx.print();
 }

 printf("-----------------------------\n");
 }

 return true;
 }
 */
