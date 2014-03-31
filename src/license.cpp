#include "license.h"
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

std::map<std::vector<unsigned char>, uint256> mapMyLicenses;
std::map<std::vector<unsigned char>, uint256> mapMyLicenses;
std::map<std::vector<unsigned char>, std::set<uint256> > mapLicensePending;
std::map<std::vector<unsigned char>, std::set<uint256> > mapLicensePending;
std::list<CLicenseFee> lstLicenseFees;
vector<vector<unsigned char> > vecLicenseIndex;

#ifdef GUI
extern std::map<uint160, std::vector<unsigned char> > mapMyLicenseHashes;
#endif

extern CLicenseDB *plicensedb;

extern uint256 SignatureHash(CScript scriptCode, const CTransaction& txTo,
		unsigned int nIn, int nHashType);

CScript RemoveLicenseScriptPrefix(const CScript& scriptIn);
bool DecodeLicenseScript(const CScript& script, int& op,
		std::vector<std::vector<unsigned char> > &vvch,
		CScript::const_iterator& pc);

extern bool Solver(const CKeyStore& keystore, const CScript& scriptPubKey,
		uint256 hash, int nHashType, CScript& scriptSigRet,
		txnouttype& whichTypeRet);
extern bool VerifyScript(const CScript& scriptSig, const CScript& scriptPubKey,
		const CTransaction& txTo, unsigned int nIn, unsigned int flags,
		int nHashType);

bool IsLicenseOp(int op) {
	return op == OP_LICENSEISSUER_NEW
			|| op == OP_LICENSEISSUER_ACTIVATE
			|| op == OP_LICENSEISSUER_UPDATE
			|| op == OP_LICENSE_NEW
			|| op == OP_LICENSE_TRANSFER;
}

// 10080 blocks = 1 week
// license expiration time is ~ 2 weeks
// expiration blocks is 20160 (final)
// expiration starts at 6720, increases by 1 per block starting at
// block 13440 until block 349440

int nStartHeight = 161280;

int64 GetLicenseNetworkFee(int seed, int nHeight) {
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
int GetLicenseIssuerExpirationDepth(int nHeight) {
	int nComputedHeight = ( nHeight - nStartHeight < 0 ) ? 1 : ( nHeight - nStartHeight ) + 1;
    if (nComputedHeight < 13440) return 6720;
    if (nComputedHeight < 26880) return nComputedHeight - 6720;
    return 20160;
}

// For display purposes, pass the name height.
int GetLicenseIssuerDisplayExpirationDepth(int nHeight) {
	int nComputedHeight = nHeight - nStartHeight < 0 ? 1 : ( nHeight - nStartHeight ) + 1;
    if (nComputedHeight < 6720) return 6720;
    return 20160;
}

bool IsMyLicense(const CTransaction& tx, const CTxOut& txout) {
	const CScript& scriptPubKey = RemoveLicenseScriptPrefix(txout.scriptPubKey);
	CScript scriptSig;
	txnouttype whichTypeRet;
	if (!Solver(*pwalletMain, scriptPubKey, 0, 0, scriptSig, whichTypeRet))
		return false;
    return true;
}

string licenseFromOp(int op) {
	switch (op) {
	case OP_LICENSEISSUER_NEW:
		return "licenseissuernew";
	case OP_LICENSEISSUER_ACTIVATE:
		return "licenseissueractivate";
	case OP_LICENSEISSUER_UPDATE:
		return "licenseissuerupdate";
	case OP_LICENSE_NEW:
		return "licenseissuernew";
	case OP_LICENSE_TRANSFER:
		return "licensetransfer";
	default:
		return "<unknown license op>";
	}
}

bool CLicenseIssuer::UnserializeFromTx(const CTransaction &tx) {
	try {
		CDataStream dsLicense(vchFromString(DecodeBase64(stringFromVch(tx.data))), SER_NETWORK, PROTOCOL_VERSION);
		dsLicense >> *this;
	} catch (std::exception &e) {
		return false;
	}
	return true;
}

void CLicenseIssuer::SerializeToTx(CTransaction &tx) {
	vector<unsigned char> vchData = vchFromString(SerializeToString());
	tx.data = vchData;
}

string CLicenseIssuer::SerializeToString() {
	// serialize license object
	CDataStream dsLicense(SER_NETWORK, PROTOCOL_VERSION);
	dsLicense << *this;
	vector<unsigned char> vchData(dsLicense.begin(), dsLicense.end());
	return EncodeBase64(vchData.data(), vchData.size());
}

//TODO implement
bool CLicenseDB::ScanLicenses(const std::vector<unsigned char>& vchLicenseIssuer, int nMax,
		std::vector<std::pair<std::vector<unsigned char>, CLicenseIssuer> >& licenseScan) {
	return true;
}

/**
 * [CLicenseDB::ReconstructLicenseIndex description]
 * @param  pindexRescan [description]
 * @return              [description]
 */
bool CLicenseDB::ReconstructLicenseIndex(CBlockIndex *pindexRescan) {
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

            // decode the license op, params, height
            bool o = DecodeLicenseTx(tx, op, nOut, vvchArgs, nHeight);
            if (!o || !IsLicenseOp(op)) continue;
            
            if (op == OP_LICENSEISSUER_NEW) continue;

            vector<unsigned char> vchLicenseIssuer = vvchArgs[0];
        
            // get the transaction
            if(!GetTransaction(tx.GetHash(), tx, txblkhash, true))
                continue;

            // attempt to read license from txn
            CLicenseIssuer txLicenseIssuer;
            CLicense txCA;
            if(!txLicenseIssuer.UnserializeFromTx(tx))
				return error("ReconstructLicenseIndex() : failed to read license from tx");

            // read license from DB if it exists
            vector<CLicenseIssuer> vtxPos;
            if (ExistsLicense(vchLicenseIssuer)) {
                if (!ReadLicense(vchLicenseIssuer, vtxPos))
                    return error("ReconstructLicenseIndex() : failed to read license from DB");
                if(vtxPos.size()!=0) {
                	txLicenseIssuer.nHeight = nHeight;
                	txLicenseIssuer.GetLicenseIssuerFromList(vtxPos);
                }
            }

            // read the license accept from db if exists
            if(op == OP_LICENSE_NEW || op == OP_LICENSE_TRANSFER) {
            	bool bReadLicense = false;
            	vector<unsigned char> vchLicense = vvchArgs[1];
	            if (ExistsLicense(vchLicense)) {
	                if (!ReadLicense(vchLicense, vchLicenseIssuer))
	                    printf("ReconstructLicenseIndex() : warning - failed to read license accept from license DB\n");
	                else bReadLicense = true;
	            }
				if(!bReadLicense && !txLicenseIssuer.GetAcceptByHash(vchLicense, txCA))
					printf("ReconstructLicenseIndex() : failed to read license accept from license\n");

				// add txn-specific values to license accept object
		        txCA.nTime = pindex->nTime;
		        txCA.txHash = tx.GetHash();
		        txCA.nHeight = nHeight;
				txLicenseIssuer.PutLicense(txCA);
			}

			// txn-specific values to license object
			txLicenseIssuer.txHash = tx.GetHash();
            txLicenseIssuer.nHeight = nHeight;
            txLicenseIssuer.PutToIssuerList(vtxPos);

            if (!WriteLicense(vchLicenseIssuer, vtxPos))
                return error("ReconstructLicenseIndex() : failed to write to license DB");
            if(op == OP_LICENSE_NEW || op == OP_LICENSE_TRANSFER)
	            if (!WriteLicense(vvchArgs[1], vvchArgs[0]))
	                return error("ReconstructLicenseIndex() : failed to write to license DB");
			
			// insert licenses fees to regenerate list, write license to
			// master index
			int64 nTheFee = GetLicenseNetFee(tx);
			InsertLicenseFee(pindex, tx.GetHash(), nTheFee);
			vecLicenseIndex.push_back(vvchArgs[0]);
	        if (!plicensedb->WriteLicenseIndex(vecLicenseIndex))
	            return error("ReconstructLicenseIndex() : failed to write index to license DB");

            if(IsLicenseTxnMine(tx)) {
                if(op == OP_LICENSE_NEW || op == OP_LICENSE_TRANSFER) {
                    mapMyLicenses[vvchArgs[1]] = tx.GetHash();
                    if(mapMyLicenses.count(vchLicenseIssuer))
                        mapMyLicenses[vchLicenseIssuer] = tx.GetHash();
                }
                else mapMyLicenses[vchLicenseIssuer] = tx.GetHash();
            }

			printf( "RECONSTRUCT LICENSE: op=%s license=%s title=%s qty=%d hash=%s height=%d fees=%llu\n",
					licenseFromOp(op).c_str(),
					stringFromVch(vvchArgs[0]).c_str(),
					stringFromVch(txLicenseIssuer.sTitle).c_str(),
					txLicenseIssuer.GetRemQty(),
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
int CheckLicenseTxnTransactionAtRelativeDepth(CBlockIndex* pindexBlock,
		const CCoins *txindex, int maxDepth) {
	for (CBlockIndex* pindex = pindexBlock;
			pindex && pindexBlock->nHeight - pindex->nHeight < maxDepth;
			pindex = pindex->pprev)
		if (pindex->nHeight == (int) txindex->nHeight)
			return pindexBlock->nHeight - pindex->nHeight;
	return -1;
}

int GetLicenseTxHashHeight(const uint256 txHash) {
	CDiskTxPos postx;
	pblocktree->ReadTxIndex(txHash, postx);
	return postx.nPos;
}

uint64 GetLicenseTxnFeeSubsidy(unsigned int nHeight) {
	vector<CLicenseFee> vo;
	plicensedb->ReadLicenseTxFees(vo);
	list<CLicenseFee> lOF(vo.begin(), vo.end());
	lstLicenseFees = lOF;

	unsigned int h12 = 360 * 12;
	unsigned int nTargetTime = 0;
	unsigned int nTarget1hrTime = 0;
	unsigned int blk1hrht = nHeight - 1;
	unsigned int blk12hrht = nHeight - 1;
	bool bFound = false;
	uint64 hr1 = 1, hr12 = 1;

	BOOST_FOREACH(CLicenseFee &nmFee, lstLicenseFees) {
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

bool InsertLicenseFee(CBlockIndex *pindex, uint256 hash, uint64 nValue) {
	unsigned int h12 = 3600 * 12;
	list<CLicenseFee> txnDup;
	CLicenseFee oFee;
	oFee.nTime = pindex->nTime;
	oFee.nHeight = pindex->nHeight;
	oFee.nFee = nValue;
	bool bFound = false;
	
	unsigned int tHeight =
			pindex->nHeight - 2880 < 0 ? 0 : pindex->nHeight - 2880;
	
	while (true) {
		if (lstLicenseFees.size() > 0
				&& (lstLicenseFees.back().nTime + h12 < pindex->nTime
						|| lstLicenseFees.back().nHeight < tHeight))
			lstLicenseFees.pop_back();
		else
			break;
	}
	BOOST_FOREACH(CLicenseFee &nmFee, lstLicenseFees) {
		if (oFee.hash == nmFee.hash
				&& oFee.nHeight == nmFee.nHeight) {
			bFound = true;
			break;
		}
	}
	if (!bFound)
		lstLicenseFees.push_front(oFee);

	return true;
}

int64 GetLicenseNetFee(const CTransaction& tx) {
	int64 nFee = 0;
	for (unsigned int i = 0; i < tx.vout.size(); i++) {
		const CTxOut& out = tx.vout[i];
		if (out.scriptPubKey.size() == 1 && out.scriptPubKey[0] == OP_RETURN)
			nFee += out.nValue;
	}
	return nFee;
}

int GetLicenseIssuerHeight(vector<unsigned char> vchLicenseIssuer) {
	vector<CLicenseIssuer> vtxPos;
	if (plicensedb->ExistsLicense(vchLicenseIssuer)) {
		if (!plicensedb->ReadLicense(vchLicenseIssuer, vtxPos))
			return error("GetLicenseIssuerHeight() : failed to read from license DB");
		if (vtxPos.empty()) return -1;
		CLicenseIssuer& txPos = vtxPos.back();
		return txPos.nHeight;
	}
	return -1;
}

// Check that the last entry in license history matches the given tx pos
bool CheckLicenseTxnTxPos(const vector<CLicenseIssuer> &vtxPos, const int txPos) {
	if (vtxPos.empty()) return false;
	CLicenseIssuer license;
	license.nHeight = txPos;
	return license.GetLicenseIssuerFromList(vtxPos);
}

int IndexOfLicenseOutput(const CTransaction& tx) {
	vector<vector<unsigned char> > vvch;
	int op, nOut;
	if (!DecodeLicenseTx(tx, op, nOut, vvch, -1))
		throw runtime_error("IndexOfLicenseOutput() : license output not found");
	return nOut;
}

bool GetNameOfLicenseTx(const CTransaction& tx, vector<unsigned char>& license) {
	if (tx.nVersion != SYSCOIN_TX_VERSION)
		return false;
	vector<vector<unsigned char> > vvchArgs;
	int op, nOut;
	if (!DecodeLicenseTx(tx, op, nOut, vvchArgs, -1))
		return error("GetNameOfLicenseTx() : could not decode a syscoin tx");

	switch (op) {
		case OP_LICENSE_NEW:
		case OP_LICENSEISSUER_ACTIVATE:
		case OP_LICENSEISSUER_UPDATE:
		case OP_LICENSE_TRANSFER:
			license = vvchArgs[0];
			return true;
	}
	return false;
}

//TODO come back here check to see how / where this is used
bool IsConflictedLicenseTx(CBlockTreeDB& txdb, const CTransaction& tx,
		vector<unsigned char>& license) {
	if (tx.nVersion != SYSCOIN_TX_VERSION)
		return false;
	vector<vector<unsigned char> > vvchArgs;
	int op, nOut, nPrevHeight;
	if (!DecodeLicenseTx(tx, op, nOut, vvchArgs, pindexBest->nHeight))
		return error("IsConflictedLicenseTx() : could not decode a syscoin tx");

	switch (op) {
	case OP_LICENSEISSUER_UPDATE:
		nPrevHeight = GetLicenseIssuerHeight(vvchArgs[0]);
		license = vvchArgs[0];
		if (nPrevHeight >= 0
				&& pindexBest->nHeight - nPrevHeight
						< GetLicenseIssuerExpirationDepth(pindexBest->nHeight))
			return true;
	}
	return false;
}

bool GetValueOfLicenseTx(const CTransaction& tx, vector<unsigned char>& value) {
	vector<vector<unsigned char> > vvch;
	int op, nOut;

	if (!DecodeLicenseTx(tx, op, nOut, vvch, -1))
		return false;

	switch (op) {
	case OP_LICENSEISSUER_NEW:
		return false;
	case OP_LICENSEISSUER_ACTIVATE:
	case OP_LICENSE_NEW:
		value = vvch[2];
		return true;
	case OP_LICENSEISSUER_UPDATE:
	case OP_LICENSE_TRANSFER:
		value = vvch[1];
		return true;
	default:
		return false;
	}
}

bool IsLicenseTxnMine(const CTransaction& tx) {
	if (tx.nVersion != SYSCOIN_TX_VERSION)
		return false;

	vector<vector<unsigned char> > vvch;
	int op, nOut;

	bool good = DecodeLicenseTx(tx, op, nOut, vvch, -1);
	if (!good) {
		error( "IsLicenseTxnMine() : no output out script in license tx %s\n",
				tx.ToString().c_str());
		return false;
	}
	if(!IsLicenseOp(op))
		return false;

	const CTxOut& txout = tx.vout[nOut];
	if (IsMyLicense(tx, txout)) {
		printf("IsLicenseTxnMine() : found my transaction %s nout %d\n",
				tx.GetHash().GetHex().c_str(), nOut);
		return true;
	}
	return false;
}

bool IsLicenseTxnMine(const CTransaction& tx, const CTxOut& txout,
		bool ignore_licenseissuernew) {
	if (tx.nVersion != SYSCOIN_TX_VERSION)
		return false;

	vector<vector<unsigned char> > vvch;
	int op;

	if (!DecodeLicenseScript(txout.scriptPubKey, op, vvch))
		return false;

	if(!IsLicenseOp(op))
		return false;

	if (ignore_licenseissuernew && op == OP_LICENSEISSUER_NEW)
		return false;

	if (IsMyLicense(tx, txout)) {
		printf("IsLicenseTxnMine() : found my transaction %s value %d\n",
				tx.GetHash().GetHex().c_str(), (int) txout.nValue);
		return true;
	}
	return false;
}

bool GetValueOfLicenseTxHash(const uint256 &txHash,
		vector<unsigned char>& vchValue, uint256& hash, int& nHeight) {
	nHeight = GetLicenseTxHashHeight(txHash);
	CTransaction tx;
	uint256 blockHash;
	if (!GetTransaction(txHash, tx, blockHash, true))
		return error("GetValueOfLicenseTxHash() : could not read tx from disk");
	if (!GetValueOfLicenseTx(tx, vchValue))
		return error("GetValueOfLicenseTxHash() : could not decode value from tx");
	hash = tx.GetHash();
	return true;
}

bool GetValueOfLicense(CLicenseDB& dbLicense, const vector<unsigned char> &vchLicenseIssuer,
		vector<unsigned char>& vchValue, int& nHeight) {
	vector<CLicenseIssuer> vtxPos;
	if (!plicensedb->ReadLicense(vchLicenseIssuer, vtxPos) || vtxPos.empty())
		return false;

	CLicenseIssuer& txPos = vtxPos.back();
	nHeight = txPos.nHeight;
	vchValue = txPos.vchRand;
	return true;
}

bool GetTxOfLicense(CLicenseDB& dbLicense, const vector<unsigned char> &vchLicenseIssuer,
		CTransaction& tx) {
	vector<CLicenseIssuer> vtxPos;
	if (!plicensedb->ReadLicense(vchLicenseIssuer, vtxPos) || vtxPos.empty())
		return false;
	CLicenseIssuer& txPos = vtxPos.back();
	int nHeight = txPos.nHeight;
	if (nHeight + GetLicenseIssuerExpirationDepth(pindexBest->nHeight)
			< pindexBest->nHeight) {
		string license = stringFromVch(vchLicenseIssuer);
		printf("GetTxOfLicense(%s) : expired", license.c_str());
		return false;
	}

	uint256 hashBlock;
	if (!GetTransaction(txPos.txHash, tx, hashBlock, true))
		return error("GetTxOfLicense() : could not read tx from disk");

	return true;
}

bool GetTxOfLicense(CLicenseDB& dbLicense, const vector<unsigned char> &vchLicense,
		CLicenseIssuer &txPos, CTransaction& tx) {
	vector<CLicenseIssuer> vtxPos;
	vector<unsigned char> vchLicenseIssuer;
	if (!plicensedb->ReadLicense(vchLicense, vchLicenseIssuer)) return false;
	if (!plicensedb->ReadLicense(vchLicenseIssuer, vtxPos)) return false;
	txPos = vtxPos.back();
	int nHeight = txPos.nHeight;
	if (nHeight + GetLicenseIssuerExpirationDepth(pindexBest->nHeight)
			< pindexBest->nHeight) {
		string license = stringFromVch(vchLicense);
		printf("GetTxOfLicense(%s) : expired", license.c_str());
		return false;
	}

	uint256 hashBlock;
	if (!GetTransaction(txPos.txHash, tx, hashBlock, true))
		return error("GetTxOfLicense() : could not read tx from disk");

	return true;
}

bool DecodeLicenseTx(const CTransaction& tx, int& op, int& nOut,
		vector<vector<unsigned char> >& vvch, int nHeight) {
	bool found = false;

	if (nHeight < 0)
		nHeight = pindexBest->nHeight;

	// Strict check - bug disallowed
	for (unsigned int i = 0; i < tx.vout.size(); i++) {
		const CTxOut& out = tx.vout[i];
		vector<vector<unsigned char> > vvchRead;
		if (DecodeLicenseScript(out.scriptPubKey, op, vvchRead)) {
			nOut = i; found = true; vvch = vvchRead;
			break;
		}
	}
	if (!found) vvch.clear();
	return found && IsLicenseOp(op);
}

bool GetValueOfLicenseTx(const CCoins& tx, vector<unsigned char>& value) {
	vector<vector<unsigned char> > vvch;

	int op, nOut;

	if (!DecodeLicenseTx(tx, op, nOut, vvch, -1))
		return false;

	switch (op) {
	case OP_LICENSEISSUER_NEW:
		return false;
	case OP_LICENSEISSUER_ACTIVATE:
	case OP_LICENSE_NEW:
		value = vvch[2];
		return true;
	case OP_LICENSEISSUER_UPDATE:
	case OP_LICENSE_TRANSFER:
		value = vvch[1];
		return true;
	default:
		return false;
	}
}

bool DecodeLicenseTx(const CCoins& tx, int& op, int& nOut,
		vector<vector<unsigned char> >& vvch, int nHeight) {
	bool found = false;

	if (nHeight < 0)
		nHeight = pindexBest->nHeight;

	// Strict check - bug disallowed
	for (unsigned int i = 0; i < tx.vout.size(); i++) {
		const CTxOut& out = tx.vout[i];
		vector<vector<unsigned char> > vvchRead;
		if (DecodeLicenseScript(out.scriptPubKey, op, vvchRead)) {
			nOut = i; found = true; vvch = vvchRead;
			break;
		}
	}
	if (!found)
		vvch.clear();
	return found;
}

bool DecodeLicenseScript(const CScript& script, int& op,
		vector<vector<unsigned char> > &vvch) {
	CScript::const_iterator pc = script.begin();
	return DecodeLicenseScript(script, op, vvch, pc);
}

bool DecodeLicenseScript(const CScript& script, int& op,
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

	if ((op == OP_LICENSEISSUER_NEW && vvch.size() == 1)
		|| (op == OP_LICENSEISSUER_ACTIVATE && vvch.size() == 3)
		|| (op == OP_LICENSEISSUER_UPDATE && vvch.size() == 2)
		|| (op == OP_LICENSE_NEW && vvch.size() == 3)
		|| (op == OP_LICENSE_TRANSFER && vvch.size() == 2))
		return true;
	return false;
}

bool SignLicenseSignature(const CTransaction& txFrom, CTransaction& txTo,
		unsigned int nIn, int nHashType = SIGHASH_ALL, CScript scriptPrereq =
				CScript()) {
	assert(nIn < txTo.vin.size());
	CTxIn& txin = txTo.vin[nIn];
	assert(txin.prevout.n < txFrom.vout.size());
	const CTxOut& txout = txFrom.vout[txin.prevout.n];

	// Leave out the signature from the hash, since a signature can't sign itself.
	// The checksig op will also drop the signatures from its hash.
	const CScript& scriptPubKey = RemoveLicenseScriptPrefix(txout.scriptPubKey);
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

bool CreateLicenseTransactionWithInputTx(
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
			printf("CreateLicenseTransactionWithInputTx: total value = %d\n",
					(int) nTotalValue);
			double dPriority = 0;

			// vouts to the payees
			BOOST_FOREACH(const PAIRTYPE(CScript, int64)& s, vecSend)
				wtxNew.vout.push_back(CTxOut(s.second, s.first));

			int64 nWtxinCredit = wtxIn.vout[nTxOut].nValue;

			// Choose coins to use
			set<pair<const CWalletTx*, unsigned int> > setCoins;
			int64 nValueIn = 0;
			printf( "CreateLicenseTransactionWithInputTx: SelectCoins(%s), nTotalValue = %s, nWtxinCredit = %s\n",
					FormatMoney(nTotalValue - nWtxinCredit).c_str(),
					FormatMoney(nTotalValue).c_str(),
					FormatMoney(nWtxinCredit).c_str());
			if (nTotalValue - nWtxinCredit > 0) {
				if (!pwalletMain->SelectCoins(nTotalValue - nWtxinCredit,
						setCoins, nValueIn))
					return false;
			}

			printf( "CreateLicenseTransactionWithInputTx: selected %d tx outs, nValueIn = %s\n",
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
					if (!SignLicenseSignature(*coin.first, wtxNew, nIn++))
						throw runtime_error("could not sign license coin output");
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
				printf( "CreateLicenseTransactionWithInputTx: re-iterating (nFreeRet = %s)\n",
						FormatMoney(nFeeRet).c_str());
				continue;
			}

			// Fill vtxPrev by copying from previous transactions vtxPrev
			wtxNew.AddSupportingTransactions();
			wtxNew.fTimeReceivedIsTxTime = true;

			break;
		}
	}

	printf("CreateLicenseTransactionWithInputTx succeeded:\n%s",
			wtxNew.ToString().c_str());
	return true;
}

int64 GetFeeAssign() {
	int64 iRet = !0;
	return  iRet<<47;
}

// nTxOut is the output from wtxIn that we should grab
string SendLicenseMoneyWithInputTx(CScript scriptPubKey, int64 nValue,
		int64 nNetFee, CWalletTx& wtxIn, CWalletTx& wtxNew, bool fAskFee,
		const string& txData) {
	int nTxOut = IndexOfLicenseOutput(wtxIn);
	CReserveKey reservekey(pwalletMain);
	int64 nFeeRequired;
	vector<pair<CScript, int64> > vecSend;
	vecSend.push_back(make_pair(scriptPubKey, nValue));

	if (nNetFee) {
		CScript scriptFee;
		scriptFee << OP_RETURN;
		vecSend.push_back(make_pair(scriptFee, nNetFee));
	}

	if (!CreateLicenseTransactionWithInputTx(vecSend, wtxIn, nTxOut, wtxNew,
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

bool GetLicenseIssuerAddress(const CTransaction& tx, std::string& strAddress) {
	int op, nOut = 0;
	vector<vector<unsigned char> > vvch;

	if (!DecodeLicenseTx(tx, op, nOut, vvch, -1))
		return error("GetLicenseIssuerAddress() : could not decode license tx.");

	const CTxOut& txout = tx.vout[nOut];

	const CScript& scriptPubKey = RemoveLicenseScriptPrefix(txout.scriptPubKey);
	strAddress = CBitcoinAddress(scriptPubKey.GetID()).ToString();
	return true;
}

bool GetLicenseIssuerAddress(const CDiskTxPos& txPos, std::string& strAddress) {
	CTransaction tx;
	if (!tx.ReadFromDisk(txPos))
		return error("GetLicenseIssuerAddress() : could not read tx from disk");
	return GetLicenseIssuerAddress(tx, strAddress);
}

CScript RemoveLicenseScriptPrefix(const CScript& scriptIn) {
	int op;
	vector<vector<unsigned char> > vvch;
	CScript::const_iterator pc = scriptIn.begin();

	if (!DecodeLicenseScript(scriptIn, op, vvch, pc))
		throw runtime_error(
				"RemoveLicenseScriptPrefix() : could not decode license script");
	return CScript(pc, scriptIn.end());
}

bool CheckLicenseTxnInputs(CBlockIndex *pindexBlock, const CTransaction &tx,
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
			if (DecodeLicenseScript(prevCoins->vout[prevOutput->n].scriptPubKey,
					prevOp, vvch)) {
				found = true; vvchPrevArgs = vvch;
				break;
			}
			if(!found)vvchPrevArgs.clear();
		}

		// Make sure license outputs are not spent by a regular transaction, or the license would be lost
		if (tx.nVersion != SYSCOIN_TX_VERSION) {
			if (found)
				return error(
						"CheckLicenseTxnInputs() : a non-syscoin transaction with a syscoin input");
			return true;
		}

		vector<vector<unsigned char> > vvchArgs;
		int op;
		int nOut;
		bool good = DecodeLicenseTx(tx, op, nOut, vvchArgs, pindexBlock->nHeight);
		if (!good)
			return error("CheckLicenseTxnInputs() : could not decode an licensecoin tx");
		int nPrevHeight;
		int nDepth;
		int64 nNetFee;

		// unserialize license object from txn, check for valid
		CLicenseIssuer theLicense;
		CLicense theLicense;
		theLicense.UnserializeFromTx(tx);
		if (theLicense.IsNull())
			error("CheckLicenseTxnInputs() : null license object");

		if (vvchArgs[0].size() > MAX_NAME_LENGTH)
			return error("license hex rand too long");

		switch (op) {
		case OP_LICENSEISSUER_NEW:

			if (found)
				return error(
						"CheckLicenseTxnInputs() : licenseissuernew tx pointing to previous syscoin tx");

			if (vvchArgs[0].size() != 20)
				return error("licenseissuernew tx with incorrect hash length");

			break;

		case OP_LICENSEISSUER_ACTIVATE:

			// check for enough fees
			nNetFee = GetLicenseNetFee(tx);
			if (nNetFee < GetLicenseNetworkFee(8, pindexBlock->nHeight)-COIN)
				return error(
						"CheckLicenseTxnInputs() : got tx %s with fee too low %lu",
						tx.GetHash().GetHex().c_str(),
						(long unsigned int) nNetFee);

			// validate conditions
			if ((!found || prevOp != OP_LICENSEISSUER_NEW) && !fJustCheck)
				return error("CheckLicenseTxnInputs() : licenseactivate tx without previous licenseissuernew tx");

			if (vvchArgs[1].size() > 20)
				return error("licenseactivate tx with rand too big");

			if (vvchArgs[2].size() > MAX_VALUE_LENGTH)
				return error("licenseactivate tx with value too long");

			if (fBlock && !fJustCheck) {
				// Check hash
				const vector<unsigned char> &vchHash = vvchPrevArgs[0];
				const vector<unsigned char> &vchLicenseIssuer = vvchArgs[0];
				const vector<unsigned char> &vchRand = vvchArgs[1];
				vector<unsigned char> vchToHash(vchRand);
				vchToHash.insert(vchToHash.end(), vchLicenseIssuer.begin(), vchLicenseIssuer.end());
				uint160 hash = Hash160(vchToHash);

				if (uint160(vchHash) != hash)
					return error(
							"CheckLicenseTxnInputs() : licenseactivate hash mismatch prev : %s cur %s",
							HexStr(stringFromVch(vchHash)).c_str(), HexStr(stringFromVch(vchToHash)).c_str());

				// min activation depth is 1
				nDepth = CheckLicenseTxnTransactionAtRelativeDepth(pindexBlock,
						prevCoins, 1);
				if ((fBlock || fMiner) && nDepth >= 0 && (unsigned int) nDepth < 1)
					return false;

				// check for previous licenseissuernew
				nDepth = CheckLicenseTxnTransactionAtRelativeDepth(pindexBlock,
						prevCoins,
						GetLicenseIssuerExpirationDepth(pindexBlock->nHeight));
				if (nDepth == -1)
					return error(
							"CheckLicenseTxnInputs() : licenseactivate cannot be mined if licenseissuernew is not already in chain and unexpired");

				nPrevHeight = GetLicenseIssuerHeight(vchLicenseIssuer);
				if (!fBlock && nPrevHeight >= 0
						&& pindexBlock->nHeight - nPrevHeight
								< GetLicenseIssuerExpirationDepth(pindexBlock->nHeight))
					return error(
							"CheckLicenseTxnInputs() : licenseactivate on an unexpired license.");

   				set<uint256>& setPending = mapLicensePending[vchLicenseIssuer];
                BOOST_FOREACH(const PAIRTYPE(uint256, uint256)& s, mapTestPool) {
                	if(s.second==tx.GetHash()) continue;
                    if (setPending.count(s.second)) {
                        printf("CheckInputs() : will not mine licenseactivate %s because it clashes with %s",
                               tx.GetHash().GetHex().c_str(),
                               s.second.GetHex().c_str());
                        return false;
                    }
                }
			}

			break;
		case OP_LICENSEISSUER_UPDATE:

			if (fBlock && fJustCheck && !found)
				return true;

			if ( !found || ( prevOp != OP_LICENSEISSUER_ACTIVATE && prevOp != OP_LICENSEISSUER_UPDATE ) )
				return error("licenseupdate previous op %s is invalid", licenseFromOp(prevOp).c_str());
			
			if (vvchArgs[1].size() > MAX_VALUE_LENGTH)
				return error("licenseupdate tx with value too long");
			
			if (vvchPrevArgs[0] != vvchArgs[0])
				return error("CheckLicenseTxnInputs() : licenseupdate license mismatch");

			// TODO CPU intensive
			nDepth = CheckLicenseTxnTransactionAtRelativeDepth(pindexBlock,
					prevCoins, GetLicenseIssuerExpirationDepth(pindexBlock->nHeight));
			if ((fBlock || fMiner) && nDepth < 0)
				return error(
						"CheckLicenseTxnInputs() : licenseupdate on an expired license, or there is a pending transaction on the license");
			
			if (fBlock && !fJustCheck) {
				set<uint256>& setPending = mapLicensePending[vvchArgs[0]];
	            BOOST_FOREACH(const PAIRTYPE(uint256, uint256)& s, mapTestPool) {
	            	if(s.second==tx.GetHash()) continue;
	                if (setPending.count(s.second)) {
	                    printf("CheckInputs() : will not mine licenseupdate %s because it clashes with %s",
	                           tx.GetHash().GetHex().c_str(),
	                           s.second.GetHex().c_str());
	                    return false;
	                }
	            }
        	}

			break;

		case OP_LICENSE_NEW:

			if (vvchArgs[1].size() > 20)
				return error("licenseaccept tx with rand too big");

			if (vvchArgs[2].size() > 20)
				return error("licenseaccept tx with value too long");

			if (fBlock && !fJustCheck) {
				// Check hash
				const vector<unsigned char> &vchLicenseIssuer = vvchArgs[0];
				const vector<unsigned char> &vchAcceptRand = vvchArgs[1];

				nPrevHeight = GetLicenseIssuerHeight(vchLicenseIssuer);

				if(!theLicense.GetAcceptByHash(vchAcceptRand, theLicense))
					return error("could not read accept from license txn");

				if(theLicense.vchRand != vchAcceptRand)
					return error("accept txn contains invalid txnaccept hash");
	   		}
			break;

		case OP_LICENSE_TRANSFER:

			// validate conditions
			if ( ( !found || prevOp != OP_LICENSE_NEW ) && !fJustCheck )
				return error("licensepay previous op %s is invalid", licenseFromOp(prevOp).c_str());

			if (vvchArgs[0].size() > 20)
				return error("licensepay tx with license hash too big");

			if (vvchArgs[1].size() > 20)
				return error("licensepay tx with license accept hash too big");

			if (fBlock && !fJustCheck) {
				// Check hash
				const vector<unsigned char> &vchLicenseIssuer = vvchArgs[0];
				const vector<unsigned char> &vchLicense = vvchArgs[1];

				// construct license accept hash
				vector<unsigned char> vchToHash(vchLicense);
				vchToHash.insert(vchToHash.end(), vchLicenseIssuer.begin(), vchLicenseIssuer.end());
				uint160 hash = Hash160(vchToHash);

				// check for previous licenseaccept
				nDepth = CheckLicenseTxnTransactionAtRelativeDepth(pindexBlock,
						prevCoins, pindexBlock->nHeight);
				if (nDepth == -1)
					return error(
							"CheckLicenseTxnInputs() : licensepay cannot be mined if licenseaccept is not already in chain");

				// check license accept hash against prev txn
				if (uint160(vvchPrevArgs[2]) != hash)
					return error(
							"CheckLicenseTxnInputs() : licensepay prev hash mismatch : %s vs %s",
							HexStr(stringFromVch(vvchPrevArgs[2])).c_str(), HexStr(stringFromVch(vchToHash)).c_str());

				nPrevHeight = GetLicenseIssuerHeight(vchLicenseIssuer);

				if(!theLicense.GetAcceptByHash(vchLicense, theLicense))
					return error("could not read accept from license txn");

				// check for enough fees
				int64 expectedFee = GetLicenseNetworkFee(4, pindexBlock->nHeight) 
				+ ((theLicense.nPrice * theLicense.nQty) / 200) - COIN;
				nNetFee = GetLicenseNetFee(tx);
				if (nNetFee < expectedFee )
					return error(
							"CheckLicenseTxnInputs() : got licensepay tx %s with fee too low %lu",
							tx.GetHash().GetHex().c_str(),
							(long unsigned int) nNetFee);

				if(theLicense.vchRand != vchLicense)
					return error("accept txn contains invalid txnaccept hash");

   				set<uint256>& setPending = mapLicensePending[vchLicense];
                BOOST_FOREACH(const PAIRTYPE(uint256, uint256)& s, mapTestPool) {
                	if(s.second==tx.GetHash()) continue;
                    if (setPending.count(s.second)) {
                       printf("CheckInputs() : will not mine licensepay %s because it clashes with %s",
                               tx.GetHash().GetHex().c_str(),
                               s.second.GetHex().c_str());
                       return false;
                    }
                }
			}

			break;

		default:
			return error( "CheckLicenseTxnInputs() : license transaction has unknown op");
		}

		// save serialized license for later use
		CLicenseIssuer serializedLicense = theLicense;

		// if not an licenseissuernew, load the license data from the DB
		vector<CLicenseIssuer> vtxPos;
		if(op != OP_LICENSEISSUER_NEW)
			if (plicensedb->ExistsLicense(vvchArgs[0])) {
				if (!plicensedb->ReadLicense(vvchArgs[0], vtxPos))
					return error(
							"CheckLicenseTxnInputs() : failed to read from license DB");
			}

		// for licenseupdate or licensepay check to make sure the previous txn exists and is valid
		if (!fBlock && fJustCheck && (op == OP_LICENSEISSUER_UPDATE || op == OP_LICENSE_TRANSFER)) {
			if (!CheckLicenseTxnTxPos(vtxPos, prevCoins->nHeight))
				return error(
						"CheckLicenseTxnInputs() : tx %s rejected, since previous tx (%s) is not in the license DB\n",
						tx.GetHash().ToString().c_str(),
						prevOutput->hash.ToString().c_str());
		}

		// these ifs are problably total bullshit except for the licenseissuernew
		if (fBlock || (!fBlock && !fMiner && !fJustCheck)) {
			if (op != OP_LICENSEISSUER_NEW) {
				if (!fMiner && !fJustCheck && pindexBlock->nHeight != pindexBest->nHeight) {
					int nHeight = pindexBlock->nHeight;

					// get the latest license from the db
                	theLicense.nHeight = nHeight;
                	theLicense.GetLicenseIssuerFromList(vtxPos);
					
					if (op == OP_LICENSE_NEW || op == OP_LICENSE_TRANSFER) {
						// get the accept out of the license object in the txn
						if(!serializedLicense.GetAcceptByHash(vvchArgs[1], theLicense))
							return error("could not read accept from license txn");

						if(op == OP_LICENSE_NEW) {
							// get the license accept qty, validate
							if(theLicense.nQty < 1 || theLicense.nQty > theLicense.GetRemQty())
								return error("invalid quantity value (nQty < 1 or nQty > remaining qty).");
						} 
						if(op == OP_LICENSE_TRANSFER) {
							theLicense.bPaid = true;
						}

						// set the license accept txn-dependent values and add to the txn
						theLicense.vchRand = vvchArgs[1];
						theLicense.txHash = tx.GetHash();
						theLicense.nTime = pindexBlock->nTime;
						theLicense.nHeight = nHeight;
						theLicense.PutLicense(theLicense);

						if (!plicensedb->WriteLicense(vvchArgs[1], vvchArgs[0]))
							return error( "CheckLicenseTxnInputs() : failed to write to license DB");
					}
					
					// set the license's txn-dependent values
					theLicense.txHash = tx.GetHash();
					theLicense.PutToIssuerList(vtxPos);

					// write license
					if (!plicensedb->WriteLicense(vvchArgs[0], vtxPos))
						return error( "CheckLicenseTxnInputs() : failed to write to license DB");

					// write license to license index if it isn't there already
					bool bFound = false;
                    BOOST_FOREACH(vector<unsigned char> &vch, vecLicenseIndex) {
                        if(vch == vvchArgs[0]) {
                            bFound = true;
                            break;
                        }
                    }
                    if(!bFound) vecLicenseIndex.push_back(vvchArgs[0]);
                    if (!plicensedb->WriteLicenseIndex(vecLicenseIndex))
                        return error("CheckLicenseTxnInputs() : failed to write index to license DB");

                    // compute verify and write fee data to DB
                    int64 nTheFee = GetLicenseNetFee(tx);
					InsertLicenseFee(pindexBlock, tx.GetHash(), nTheFee);
					if(nTheFee > 0) printf("LICENSE FEES: Added %lf in fees to track for regeneration.\n", (double) nTheFee / COIN);
					vector<CLicenseFee> vLicenseFees(lstLicenseFees.begin(), lstLicenseFees.end());
					if (!plicensedb->WriteLicenseTxFees(vLicenseFees))
						return error( "CheckLicenseTxnInputs() : failed to write fees to license DB");

					// debug
					printf( "CONNECTED LICENSE: op=%s license=%s title=%s qty=%d hash=%s height=%d fees=%llu\n",
							licenseFromOp(op).c_str(),
							stringFromVch(vvchArgs[0]).c_str(),
							stringFromVch(theLicense.sTitle).c_str(),
							theLicense.GetRemQty(),
							tx.GetHash().ToString().c_str(), 
							nHeight, nTheFee / COIN);
				}
			}

			if (pindexBlock->nHeight != pindexBest->nHeight) {
				// activate or update - seller txn
				if (op == OP_LICENSEISSUER_NEW || op == OP_LICENSEISSUER_ACTIVATE || op == OP_LICENSEISSUER_UPDATE) {
					vector<unsigned char> vchLicenseIssuer = op == OP_LICENSEISSUER_NEW ? vchFromString(HexStr(vvchArgs[0])) : vvchArgs[0];
					LOCK(cs_main);
					std::map<std::vector<unsigned char>, std::set<uint256> >::iterator mi = mapLicensePending.find(vchLicenseIssuer);
					if (mi != mapLicensePending.end())
						mi->second.erase(tx.GetHash());
				}

				// accept or pay - buyer txn
				else if (op == OP_LICENSE_NEW || op == OP_LICENSE_TRANSFER) {
					LOCK(cs_main);
					std::map<std::vector<unsigned char>, std::set<uint256> >::iterator mi = mapLicensePending.find(vvchArgs[1]);
					if (mi != mapLicensePending.end())
						mi->second.erase(tx.GetHash());
				}
			}
		}
	}
	return true;
}

bool ExtractLicenseAddress(const CScript& script, string& address) {
	if (script.size() == 1 && script[0] == OP_RETURN) {
		address = string("network fee");
		return true;
	}
	vector<vector<unsigned char> > vvch;
	int op;
	if (!DecodeLicenseScript(script, op, vvch))
		return false;

	string strOp = licenseFromOp(op);
	string strLicense;
	if (op == OP_LICENSEISSUER_NEW) {
#ifdef GUI
		LOCK(cs_main);

		std::map<uint160, std::vector<unsigned char> >::const_iterator mi = mapMyLicenseHashes.find(uint160(vvch[0]));
		if (mi != mapMyLicenseHashes.end())
		strLicense = stringFromVch(mi->second);
		else
#endif
		strLicense = HexStr(vvch[0]);
	} 
	else
		strLicense = stringFromVch(vvch[0]);

	address = strOp + ": " + strLicense;
	return true;
}

void rescanforlicenses(CBlockIndex *pindexRescan) {
    printf("Scanning blockchain for licenses to create fast index...\n");
    plicensedb->ReconstructLicenseIndex(pindexRescan);
}

int GetLicenseTxPosHeight(const CDiskTxPos& txPos) {
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

int GetLicenseTxPosHeight2(const CDiskTxPos& txPos, int nHeight) {
    nHeight = GetLicenseTxPosHeight(txPos);
    return nHeight;
}

Value licenseissuernew(const Array& params, bool fHelp) {
	if (fHelp || params.size() < 5 || params.size() > 6)
		throw runtime_error(
				"licenseissuernew [<address>] <category> <title> <quantity> <price> [<description>]\n"
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

    // 64Kbyte license desc. maxlen
	if (vchDesc.size() > 1024 * 64)
		throw JSONRPCError(RPC_INVALID_PARAMETERa, "License description is too long.");

	// set wallet tx ver
	CWalletTx wtx;
	wtx.nVersion = SYSCOIN_TX_VERSION;

	// generate rand identifier
	uint64 rand = GetRand((uint64) -1);
	vector<unsigned char> vchRand = CBigNum(rand).getvch();
	vector<unsigned char> vchLicenseIssuer = vchFromString(HexStr(vchRand));
	vector<unsigned char> vchToHash(vchRand);
	vchToHash.insert(vchToHash.end(), vchLicenseIssuer.begin(), vchLicenseIssuer.end());
	uint160 licenseHash = Hash160(vchToHash);

	// build license object
	CLicenseIssuer newLicense;
	newLicense.vchRand = vchRand;
	newLicense.vchPaymentAddress = vchPaymentAddress;
	newLicense.sCategory = vchCat;
	newLicense.sTitle = vchTitle;
	newLicense.sDescription = vchDesc;
	newLicense.nQty = nQty;
	newLicense.nPrice = nPrice;

	string bdata = newLicense.SerializeToString();

	// create transaction keys
	CPubKey newDefaultKey;
	pwalletMain->GetKeyFromPool(newDefaultKey, false);
	CScript scriptPubKeyOrig;
	scriptPubKeyOrig.SetDestination(newDefaultKey.GetID());
	CScript scriptPubKey;
	scriptPubKey << CScript::EncodeOP_N(OP_LICENSEISSUER_NEW) << licenseHash << OP_2DROP;
	scriptPubKey += scriptPubKeyOrig;

	// send transaction
	{
		LOCK(cs_main);
		EnsureWalletIsUnlocked();
		string strError = pwalletMain->SendMoney(scriptPubKey, MIN_AMOUNT, wtx,
				false, bdata);
		if (strError != "")
			throw JSONRPCError(RPC_WALLET_ERROR, strError);
		mapMyLicenses[vchLicenseIssuer] = wtx.GetHash();
	}
	printf("SENT:LICENSENEW : title=%s, rand=%s, tx=%s, data:\n%s\n",
			stringFromVch(vchTitle).c_str(), stringFromVch(vchLicenseIssuer).c_str(),
			wtx.GetHash().GetHex().c_str(), bdata.c_str());

	// return results
	vector<Value> res;
	res.push_back(wtx.GetHash().GetHex());
	res.push_back(HexStr(vchRand));

	return res;
}

Value licenseactivate(const Array& params, bool fHelp) {
	if (fHelp || params.size() < 1 || params.size() > 2)
		throw runtime_error(
				"licenseactivate <rand> [<tx>]\n"
						"Activate an license after creating it with licenseissuernew.\n"
						+ HelpRequiringPassphrase());

	// gather inputs
	vector<unsigned char> vchRand = ParseHex(params[0].get_str());
	vector<unsigned char> vchLicenseIssuer = vchFromValue(params[0]);

	// this is a syscoin transaction
	CWalletTx wtx;
	wtx.nVersion = SYSCOIN_TX_VERSION;

	// check for existing pending licenses
	{
		LOCK2(cs_main, pwalletMain->cs_wallet);
		if (mapLicensePending.count(vchLicenseIssuer)
				&& mapLicensePending[vchLicenseIssuer].size()) {
			error( "licenseactivate() : there are %d pending operations on that license, including %s",
				   (int) mapLicensePending[vchLicenseIssuer].size(),
				   mapLicensePending[vchLicenseIssuer].begin()->GetHex().c_str());
			throw runtime_error("there are pending operations on that license");
		}

		// look for an license with identical hex rand keys. wont happen.
		CTransaction tx;
		if (GetTxOfLicense(*plicensedb, vchLicenseIssuer, tx)) {
			error( "licenseactivate() : this license is already active with tx %s",
				   tx.GetHash().GetHex().c_str());
			throw runtime_error("this license is already active");
		}

		EnsureWalletIsUnlocked();

		// Make sure there is a previous licenseissuernew tx on this license and that the random value matches
		uint256 wtxInHash;
		if (params.size() == 1) {
			if (!mapMyLicenses.count(vchLicenseIssuer))
				throw runtime_error(
						"could not find a coin with this license, try specifying the licenseissuernew transaction id");
			wtxInHash = mapMyLicenses[vchLicenseIssuer];
		} else
			wtxInHash.SetHex(params[1].get_str());
		if (!pwalletMain->mapWallet.count(wtxInHash))
			throw runtime_error("previous transaction is not in the wallet");

		// verify previous txn was licenseissuernew
		CWalletTx& wtxIn = pwalletMain->mapWallet[wtxInHash];
		vector<unsigned char> vchHash;
		bool found = false;
		BOOST_FOREACH(CTxOut& out, wtxIn.vout) {
			vector<vector<unsigned char> > vvch;
			int op;
			if (DecodeLicenseScript(out.scriptPubKey, op, vvch)) {
				if (op != OP_LICENSEISSUER_NEW)
					throw runtime_error(
							"previous transaction wasn't a licenseissuernew");
				vchHash = vvch[0]; found = true;
				break;
			}
		}
		if (!found)
			throw runtime_error("Could not decode license transaction");

		// calculate network fees
		int64 nNetFee = GetLicenseNetworkFee(8, pindexBest->nHeight);

		// unserialize license object from txn, serialize back
		CLicenseIssuer newLicense;
		if(!newLicense.UnserializeFromTx(wtxIn))
			throw runtime_error(
					"could not unserialize license from txn");

		newLicense.nFee = nNetFee;

		string bdata = newLicense.SerializeToString();
		vector<unsigned char> vchbdata = vchFromString(bdata);

		// check this hash against previous, ensure they match
		vector<unsigned char> vchToHash(vchRand);
		vchToHash.insert(vchToHash.end(), vchLicenseIssuer.begin(), vchLicenseIssuer.end());
		uint160 hash = Hash160(vchToHash);
		if (uint160(vchHash) != hash)
			throw runtime_error("previous tx used a different random value");

		//create licenseactivate txn keys
		CPubKey newDefaultKey;
		pwalletMain->GetKeyFromPool(newDefaultKey, false);
		CScript scriptPubKeyOrig;
		scriptPubKeyOrig.SetDestination(newDefaultKey.GetID());
		CScript scriptPubKey;
		scriptPubKey << CScript::EncodeOP_N(OP_LICENSEISSUER_ACTIVATE) << vchLicenseIssuer
				<< vchRand << newLicense.sTitle << OP_2DROP << OP_2DROP;
		scriptPubKey += scriptPubKeyOrig;

		// send the tranasction
		string strError = SendLicenseMoneyWithInputTx(scriptPubKey, MIN_AMOUNT,
				nNetFee, wtxIn, wtx, false, bdata);
		if (strError != "")
			throw JSONRPCError(RPC_WALLET_ERROR, strError);

		printf("SENT:LICENSEACTIVATE: title=%s, rand=%s, tx=%s, data:\n%s\n",
				stringFromVch(newLicense.sTitle).c_str(),
				stringFromVch(vchLicenseIssuer).c_str(), wtx.GetHash().GetHex().c_str(),
				stringFromVch(vchbdata).c_str() );
	}
	return wtx.GetHash().GetHex();
}

Value licenseupdate(const Array& params, bool fHelp) {
	if (fHelp || params.size() < 5 || params.size() > 6)
		throw runtime_error(
				"licenseupdate <rand> <category> <title> <quantity> <price> [<description>]\n"
						"Perform an update on an license you control.\n"
						+ HelpRequiringPassphrase());

	// gather & validate inputs
	vector<unsigned char> vchRand = ParseHex(params[0].get_str());
	vector<unsigned char> vchLicenseIssuer = vchFromValue(params[0]);
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

	// create LICENSEUPDATE txn keys
	CScript scriptPubKey;
	scriptPubKey << CScript::EncodeOP_N(OP_LICENSEISSUER_UPDATE) << vchLicenseIssuer << vchTitle
			<< OP_2DROP << OP_DROP;
	scriptPubKey += scriptPubKeyOrig;

	{
		LOCK2(cs_main, pwalletMain->cs_wallet);

		if (mapLicensePending.count(vchLicenseIssuer)
				&& mapLicensePending[vchLicenseIssuer].size()) 
			throw runtime_error("there are pending operations on that license");

		EnsureWalletIsUnlocked();

		// look for a transaction with this key
		CTransaction tx;
		if (!GetTxOfLicense(*plicensedb, vchLicenseIssuer, tx))
			throw runtime_error("could not find an license with this name");

		// make sure license is in wallet
		uint256 wtxInHash = tx.GetHash();
		if (!pwalletMain->mapWallet.count(wtxInHash)) 
			throw runtime_error("this license is not in your wallet");
		
		// unserialize license object from txn
		CLicenseIssuer theLicense;
		if(!theLicense.UnserializeFromTx(tx))
			throw runtime_error("cannot unserialize license from txn");

		// get the license from DB
		vector<CLicenseIssuer> vtxPos;
		if (!plicensedb->ReadLicense(vchLicenseIssuer, vtxPos))
			throw runtime_error("could not read license from DB");
		theLicense = vtxPos.back();

		// calculate network fees
		int64 nNetFee = GetLicenseNetworkFee(4, pindexBest->nHeight);
		if(qty > 0) nNetFee += (price * qty) / 200;

		// update license values
		theLicense.sCategory = vchCat;
		theLicense.sTitle = vchTitle;
		theLicense.sDescription = vchDesc;
		if(theLicense.GetRemQty() + qty >= 0)
			theLicense.nQty += qty;
		theLicense.nPrice = price;
		theLicense.nFee += nNetFee;

		// serialize license object
		string bdata = theLicense.SerializeToString();

		CWalletTx& wtxIn = pwalletMain->mapWallet[wtxInHash];
		string strError = SendLicenseMoneyWithInputTx(scriptPubKey, MIN_AMOUNT, nNetFee,
				wtxIn, wtx, false, bdata);
		if (strError != "")
			throw JSONRPCError(RPC_WALLET_ERROR, strError);
	}
	return wtx.GetHash().GetHex();
}

Value licenseaccept(const Array& params, bool fHelp) {
	if (fHelp || params.size() < 1 || params.size() > 2)
		throw runtime_error("licenseaccept <rand> [<quantity]>\n"
				"Accept an license.\n" + HelpRequiringPassphrase());

	vector<unsigned char> vchLicenseIssuer = vchFromValue(params[0]);
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

	// generate license accept identifier and hash
	uint64 rand = GetRand((uint64) -1);
	vector<unsigned char> vchAcceptRand = CBigNum(rand).getvch();
	vector<unsigned char> vchAccept = vchFromString(HexStr(vchAcceptRand));
	vector<unsigned char> vchToHash(vchAcceptRand);
	vchToHash.insert(vchToHash.end(), vchLicenseIssuer.begin(), vchLicenseIssuer.end());
	uint160 acceptHash = Hash160(vchToHash);

	// get a key from our wallet set dest as ourselves
	CPubKey newDefaultKey;
	pwalletMain->GetKeyFromPool(newDefaultKey, false);
	scriptPubKeyOrig.SetDestination(newDefaultKey.GetID());

	// create LICENSEACCEPT txn keys
	CScript scriptPubKey;
	scriptPubKey << CScript::EncodeOP_N(OP_LICENSE_NEW)
			<< vchLicenseIssuer << vchAcceptRand << acceptHash << OP_2DROP << OP_2DROP;
	scriptPubKey += scriptPubKeyOrig;
	{
		LOCK2(cs_main, pwalletMain->cs_wallet);

		if (mapLicensePending.count(vchLicenseIssuer)
				&& mapLicensePending[vchLicenseIssuer].size()) {
			error(  "licenseaccept() : there are %d pending operations on that license, including %s",
					(int) mapLicensePending[vchLicenseIssuer].size(),
					mapLicensePending[vchLicenseIssuer].begin()->GetHex().c_str());
			throw runtime_error("there are pending operations on that license");
		}

		EnsureWalletIsUnlocked();

		// look for a transaction with this key
		CTransaction tx;
		if (!GetTxOfLicense(*plicensedb, vchLicenseIssuer, tx))
			throw runtime_error("could not find an license with this identifier");

		// unserialize license object from txn
		CLicenseIssuer theLicense;
		if(!theLicense.UnserializeFromTx(tx))
			throw runtime_error("could not unserialize license from txn");

		// get the license id from DB
		vector<CLicenseIssuer> vtxPos;
		if (!plicensedb->ReadLicense(vchLicenseIssuer, vtxPos))
			throw runtime_error("could not read license with this name from DB");
		theLicense = vtxPos.back();

		if(theLicense.GetRemQty() < nQty)
			throw runtime_error("not enough remaining quantity to fulfill this orderaccept");

		// create accept object
		CLicense txAccept;
		txAccept.vchRand = vchAcceptRand;
		txAccept.nQty = nQty;
		txAccept.nPrice = theLicense.nPrice;
		theLicense.accepts.clear();
		theLicense.PutLicense(txAccept);

		// serialize license object
		string bdata = theLicense.SerializeToString();

		string strError = pwalletMain->SendMoney(scriptPubKey, MIN_AMOUNT, wtx,
				false, bdata);
		if (strError != "")
			throw JSONRPCError(RPC_WALLET_ERROR, strError);
		mapMyLicenses[vchAcceptRand] = wtx.GetHash();
	}
	// return results
	vector<Value> res;
	res.push_back(wtx.GetHash().GetHex());
	res.push_back(HexStr(vchAcceptRand));

	return res;
}

Value licensepay(const Array& params, bool fHelp) {
	if (fHelp || 2 != params.size())
		throw runtime_error("licensepay <rand> <message>\n"
				"Pay for a confirmed accepted license.\n"
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

	if (mapLicensePending.count(vchRand)
			&& mapLicensePending[vchRand].size()) 
		throw runtime_error( "licensepay() : there are pending operations on that license" );

	EnsureWalletIsUnlocked();

	// look for a transaction with this key, also returns
	// an license object if it is found
	CTransaction tx;
	CLicenseIssuer theLicense;
	CLicense theLicense;
	if (!GetTxOfLicense(*plicensedb, vchRand, theLicense, tx))
		throw runtime_error("could not find an license with this name");

	// check to see if license accept in wallet
	uint256 wtxInHash = tx.GetHash();
	if (!pwalletMain->mapWallet.count(wtxInHash)) 
		throw runtime_error("licensepay() : license accept is not in your wallet" );

	// check that prev txn contains license
	if(!theLicense.UnserializeFromTx(tx))
		throw runtime_error("could not unserialize license from txn");

	// get the license accept from license
	if(!theLicense.GetAcceptByHash(vchRand, theLicense))
		throw runtime_error("could not find an license accept with this name");

	// get the license id from DB
	vector<unsigned char> vchLicenseIssuer;
	vector<CLicenseIssuer> vtxPos;
	if (!plicensedb->ReadLicense(vchRand, vchLicenseIssuer))
		throw runtime_error("could not read license of accept from DB");
	if (!plicensedb->ReadLicense(vchLicenseIssuer, vtxPos))
		throw runtime_error("could not read license with this name from DB");

	// hashes should match
	if(vtxPos.back().vchRand != theLicense.vchRand)
		throw runtime_error("license hash mismatch.");

	// use the license and accept from the DB as basis
    theLicense = vtxPos.back();
    if(!theLicense.GetAcceptByHash(vchRand, theLicense))
		throw runtime_error("could not find an license accept with this hash in DB");

	// check if paid already
	if(theLicense.bPaid)
		throw runtime_error("This license accept has already been successfully paid.");

	// get a key from our wallet set dest as ourselves
	CPubKey newDefaultKey;
	pwalletMain->GetKeyFromPool(newDefaultKey, false);
	scriptPubKeyOrig.SetDestination(newDefaultKey.GetID());

	// create LICENSEPAY txn keys
	CScript scriptPubKey;
	scriptPubKey << CScript::EncodeOP_N(OP_LICENSE_TRANSFER) << vchLicenseIssuer << vchRand << OP_2DROP << OP_DROP;
	scriptPubKey += scriptPubKeyOrig;

    // make sure wallet is unlocked
    if (pwalletMain->IsLocked()) throw JSONRPCError(RPC_WALLET_UNLOCK_NEEDED, 
    	"Error: Please enter the wallet passphrase with walletpassphrase first.");

    // Check for sufficient funds to pay for order
    set<pair<const CWalletTx*,unsigned int> > setCoins;
    int64 nValueIn = 0;
    uint64 nTotalValue = ( theLicense.nPrice * theLicense.nQty );
    int64 nNetFee = GetLicenseNetworkFee(4, pindexBest->nHeight) + (nTotalValue / 200);
    if (!pwalletMain->SelectCoins(nTotalValue + nNetFee, setCoins, nValueIn)) 
        throw runtime_error("insufficient funds to pay for license");

    theLicense.vchRand = vchRand;
    theLicense.txPayId = wtxPay.GetHash();
    theLicense.vchMessage = vchMessage;
    theLicense.nFee = nNetFee;
    theLicense.accepts.clear();
    theLicense.PutLicense(theLicense);
    printf("licenseaccept msg %s\n", stringFromVch(theLicense.vchMessage).c_str());

    // add a copy of the license object with just
    // the one accept object to payment txn to identify
    // this txn as an license payment
    CLicenseIssuer licenseCopy = theLicense;
    CLicense licenseAcceptCopy = theLicense;
    licenseAcceptCopy.bPaid = true;
    licenseCopy.accepts.clear();
    licenseCopy.PutLicense(licenseAcceptCopy);

    // send payment to license address
    CBitcoinAddress address(stringFromVch(theLicense.vchPaymentAddress));
    string strError = pwalletMain->SendMoneyToDestination(address.Get(), nTotalValue, 
    	wtxPay, false, licenseCopy.SerializeToString());
    if (strError != "") throw JSONRPCError(RPC_WALLET_ERROR, strError);

	// send the license pay txn 
	CWalletTx& wtxIn = pwalletMain->mapWallet[wtxInHash];
	strError = SendLicenseMoneyWithInputTx(scriptPubKey, MIN_AMOUNT, nNetFee,
			wtxIn, wtx, false, theLicense.SerializeToString());
	if (strError != "")
		throw JSONRPCError(RPC_WALLET_ERROR, strError);
	}
	mapMyLicenses[vchRand] = wtx.GetHash();
	
	// return results
	vector<Value> res;
	res.push_back(wtxPay.GetHash().GetHex());
	res.push_back(wtx.GetHash().GetHex());

	return res;
}

Value licenseinfo(const Array& params, bool fHelp) {
	if (fHelp || 1 != params.size())
		throw runtime_error("licenseinfo <rand>\n"
				"Show values of an license.\n");

	Object oLastLicense;
	vector<unsigned char> vchLicenseIssuer = vchFromValue(params[0]);
	string license = stringFromVch(vchLicenseIssuer);
	{
		LOCK(pwalletMain->cs_wallet);
		vector<CLicenseIssuer> vtxPos;
		if (!plicensedb->ReadLicense(vchLicenseIssuer, vtxPos))
			throw JSONRPCError(RPC_WALLET_ERROR,
					"failed to read from license DB");

		if (vtxPos.size() < 1)
			throw JSONRPCError(RPC_WALLET_ERROR, "no result returned");

		uint256 blockHash;
		uint256 txHash = vtxPos[vtxPos.size() - 1].txHash;
		CTransaction tx;
		if (!GetTransaction(txHash, tx, blockHash, true))
			throw JSONRPCError(RPC_WALLET_ERROR,
					"failed to read transaction from disk");

		CLicenseIssuer theLicense;
		if(!theLicense.UnserializeFromTx(tx))
			throw JSONRPCError(RPC_WALLET_ERROR,
					"failed to unserialize license from transaction");

		theLicense = vtxPos.back();

		Object oLicense;
		vector<unsigned char> vchValue;
		Array aoLicenses;
		for(unsigned int i=0;i<theLicense.accepts.size();i++) {
			CLicense ca = theLicense.accepts[i];
			Object oLicense;
			oLicense.push_back(Pair("id", HexStr(ca.vchRand)));
			oLicense.push_back(Pair("txid", ca.txHash.GetHex()));
			oLicense.push_back(Pair("height", (double)ca.nHeight));
			oLicense.push_back(Pair("time", ca.nTime));
			oLicense.push_back(Pair("quantity", ca.nQty));
			oLicense.push_back(Pair("price", (double)ca.nPrice / COIN));
			oLicense.push_back(Pair("paid", ca.bPaid?"true":"false"));
			if(ca.bPaid) {
				oLicense.push_back(Pair("fee", (double)ca.nFee / COIN));
				oLicense.push_back(Pair("paytxid", ca.txPayId.GetHex()));
				oLicense.push_back(Pair("message", stringFromVch(ca.vchMessage)));
			}
			aoLicenses.push_back(oLicense);
		}
		int nHeight;
		uint256 licenseHash;
		if (GetValueOfLicenseTxHash(txHash, vchValue, licenseHash, nHeight)) {
			oLicense.push_back(Pair("id", license));
			oLicense.push_back(Pair("txid", tx.GetHash().GetHex()));
			string strAddress = "";
			GetLicenseIssuerAddress(tx, strAddress);
			oLicense.push_back(Pair("address", strAddress));
			oLicense.push_back(
					Pair("expires_in",
							nHeight + GetLicenseIssuerDisplayExpirationDepth(nHeight)
									- pindexBest->nHeight));
			if (nHeight + GetLicenseIssuerDisplayExpirationDepth(nHeight)
					- pindexBest->nHeight <= 0) {
				oLicense.push_back(Pair("expired", 1));
			}
			oLicense.push_back(Pair("payment_address", stringFromVch(theLicense.vchPaymentAddress)));
			oLicense.push_back(Pair("category", stringFromVch(theLicense.sCategory)));
			oLicense.push_back(Pair("title", stringFromVch(theLicense.sTitle)));
			oLicense.push_back(Pair("quantity", theLicense.GetRemQty()));
			oLicense.push_back(Pair("price", (double)theLicense.nPrice / COIN));
			oLicense.push_back(Pair("fee", (double)theLicense.nFee / COIN));
			oLicense.push_back(Pair("description", stringFromVch(theLicense.sDescription)));
			oLicense.push_back(Pair("accepts", aoLicenses));
			oLastLicense = oLicense;
		}
	}
	return oLastLicense;

}

Value licenselist(const Array& params, bool fHelp) {
	if (fHelp || 1 != params.size())
		throw runtime_error("licenselist [<license>]\n"
				"list my own licenses");

	vector<unsigned char> vchLicenseIssuer;
	vector<unsigned char> vchLastLicense;

	if (params.size() == 1)
		vchLicenseIssuer = vchFromValue(params[0]);

	vector<unsigned char> vchLicenseUniq;
	if (params.size() == 1)
		vchLicenseUniq = vchFromValue(params[0]);

	Array oRes;
	map<vector<unsigned char>, int> vLicensesI;
	map<vector<unsigned char>, Object> vLicensesO;

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

			// license
			if (!GetNameOfLicenseTx(tx, vchLicenseIssuer))
				continue;
			if (vchLicenseUniq.size() > 0 && vchLicenseUniq != vchLicenseIssuer)
				continue;

			// value
			if (!GetValueOfLicenseTx(tx, vchValue))
				continue;

			// height
			nHeight = GetLicenseTxPosHeight(txindex);

			Object oLicense;
			oLicense.push_back(Pair("license", stringFromVch(vchLicenseIssuer)));
			oLicense.push_back(Pair("value", stringFromVch(vchValue)));
			if (!IsLicenseTxnMine(pwalletMain->mapWallet[tx.GetHash()]))
				oLicense.push_back(Pair("transferred", 1));
			string strAddress = "";
			GetLicenseIssuerAddress(tx, strAddress);
			oLicense.push_back(Pair("address", strAddress));
			oLicense.push_back(
					Pair("expires_in",
							nHeight + GetLicenseIssuerDisplayExpirationDepth(nHeight)
									- pindexBest->nHeight));
			if (nHeight + GetLicenseIssuerDisplayExpirationDepth(nHeight)
					- pindexBest->nHeight <= 0) {
				oLicense.push_back(Pair("expired", 1));
			}

			// get last active license only
			if (vLicensesI.find(vchLicenseIssuer) != vLicensesI.end()
					&& vLicensesI[vchLicenseIssuer] > nHeight)
				continue;

			vLicensesI[vchLicenseIssuer] = nHeight;
			vLicensesO[vchLicenseIssuer] = oLicense;
		}

	}

	BOOST_FOREACH(const PAIRTYPE(vector<unsigned char>, Object)& item, vLicensesO)
		oRes.push_back(item.second);

	return oRes;

	return (double) 0;
}

Value licensehistory(const Array& params, bool fHelp) {
	if (fHelp || 1 != params.size())
		throw runtime_error("licensehistory <license>\n"
				"List all stored values of an license.\n");

	Array oRes;
	vector<unsigned char> vchLicenseIssuer = vchFromValue(params[0]);
	string license = stringFromVch(vchLicenseIssuer);

	{
		LOCK(pwalletMain->cs_wallet);

		//vector<CDiskTxPos> vtxPos;
		vector<CLicenseIssuer> vtxPos;
		//CLicenseDB dbLicense("r");
		if (!plicensedb->ReadLicense(vchLicenseIssuer, vtxPos))
			throw JSONRPCError(RPC_WALLET_ERROR,
					"failed to read from license DB");

		CLicenseIssuer txPos2;
		uint256 txHash;
		uint256 blockHash;
		BOOST_FOREACH(txPos2, vtxPos) {
			txHash = txPos2.txHash;
			CTransaction tx;
			if (!GetTransaction(txHash, tx, blockHash, true)) {
				error("could not read txpos");
				continue;
			}

			Object oLicense;
			vector<unsigned char> vchValue;
			int nHeight;
			uint256 hash;
			if (GetValueOfLicenseTxHash(txHash, vchValue, hash, nHeight)) {
				oLicense.push_back(Pair("license", license));
				string value = stringFromVch(vchValue);
				oLicense.push_back(Pair("value", value));
				oLicense.push_back(Pair("txid", tx.GetHash().GetHex()));
				string strAddress = "";
				GetLicenseIssuerAddress(tx, strAddress);
				oLicense.push_back(Pair("address", strAddress));
				oLicense.push_back(
						Pair("expires_in",
								nHeight + GetLicenseIssuerDisplayExpirationDepth(nHeight)
										- pindexBest->nHeight));
				if (nHeight + GetLicenseIssuerDisplayExpirationDepth(nHeight)
						- pindexBest->nHeight <= 0) {
					oLicense.push_back(Pair("expired", 1));
				}
				oRes.push_back(oLicense);
			}
		}
	}
	return oRes;
}

Value licensefilter(const Array& params, bool fHelp) {
	if (fHelp || params.size() > 5)
		throw runtime_error(
				"licensefilter [[[[[regexp] maxage=36000] from=0] nb=0] stat]\n"
						"scan and filter licensees\n"
						"[regexp] : apply [regexp] on licensees, empty means all licensees\n"
						"[maxage] : look in last [maxage] blocks\n"
						"[from] : show results from number [from]\n"
						"[nb] : show [nb] results, 0 means all\n"
						"[stats] : show some stats instead of results\n"
						"licensefilter \"\" 5 # list licensees updated in last 5 blocks\n"
						"licensefilter \"^license\" # list all licensees starting with \"license\"\n"
						"licensefilter 36000 0 0 stat # display stats (number of licenses) on active licensees\n");

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

	//CLicenseDB dbLicense("r");
	Array oRes;

	vector<unsigned char> vchLicenseIssuer;
	vector<pair<vector<unsigned char>, CLicenseIssuer> > licenseScan;
	if (!plicensedb->ScanLicenses(vchLicenseIssuer, 100000000, licenseScan))
		throw JSONRPCError(RPC_WALLET_ERROR, "scan failed");

	pair<vector<unsigned char>, CLicenseIssuer> pairScan;
	BOOST_FOREACH(pairScan, licenseScan) {
		string license = stringFromVch(pairScan.first);

		// regexp
		using namespace boost::xpressive;
		smatch licenseparts;
		sregex cregex = sregex::compile(strRegexp);
		if (strRegexp != "" && !regex_search(license, licenseparts, cregex))
			continue;

		CLicenseIssuer txLicenseIssuer = pairScan.second;
		int nHeight = txLicenseIssuer.nHeight;

		// max age
		if (nMaxAge != 0 && pindexBest->nHeight - nHeight >= nMaxAge)
			continue;

		// from limits
		nCountFrom++;
		if (nCountFrom < nFrom + 1)
			continue;

		Object oLicense;
		oLicense.push_back(Pair("license", license));
		CTransaction tx;
		uint256 blockHash;
		uint256 txHash = txLicenseIssuer.txHash;
		if ((nHeight + GetLicenseIssuerDisplayExpirationDepth(nHeight) - pindexBest->nHeight
				<= 0) || !GetTransaction(txHash, tx, blockHash, true)) {
			oLicense.push_back(Pair("expired", 1));
		} else {
			vector<unsigned char> vchValue = txLicenseIssuer.sTitle;
			string value = stringFromVch(vchValue);
			oLicense.push_back(Pair("value", value));
			oLicense.push_back(
					Pair("expires_in",
							nHeight + GetLicenseIssuerDisplayExpirationDepth(nHeight)
									- pindexBest->nHeight));
		}
		oRes.push_back(oLicense);

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

Value licensescan(const Array& params, bool fHelp) {
	if (fHelp || 2 > params.size())
		throw runtime_error(
				"licensescan [<start-license>] [<max-returned>]\n"
						"scan all licenses, starting at start-license and returning a maximum number of entries (default 500)\n");

	vector<unsigned char> vchLicenseIssuer;
	int nMax = 500;
	if (params.size() > 0) {
		vchLicenseIssuer = vchFromValue(params[0]);
	}

	if (params.size() > 1) {
		Value vMax = params[1];
		ConvertTo<double>(vMax);
		nMax = (int) vMax.get_real();
	}

	//CLicenseDB dbLicense("r");
	Array oRes;

	vector<pair<vector<unsigned char>, CLicenseIssuer> > licenseScan;
	if (!plicensedb->ScanLicenses(vchLicenseIssuer, nMax, licenseScan))
		throw JSONRPCError(RPC_WALLET_ERROR, "scan failed");

	pair<vector<unsigned char>, CLicenseIssuer> pairScan;
	BOOST_FOREACH(pairScan, licenseScan) {
		Object oLicense;
		string license = stringFromVch(pairScan.first);
		oLicense.push_back(Pair("license", license));
		CTransaction tx;
		CLicenseIssuer txLicenseIssuer = pairScan.second;
		uint256 blockHash;

		int nHeight = txLicenseIssuer.nHeight;
		vector<unsigned char> vchValue = txLicenseIssuer.sTitle;
		if ((nHeight + GetLicenseIssuerDisplayExpirationDepth(nHeight) - pindexBest->nHeight
				<= 0) || !GetTransaction(txLicenseIssuer.txHash, tx, blockHash, true)) {
			oLicense.push_back(Pair("expired", 1));
		} else {
			string value = stringFromVch(vchValue);
			//string strAddress = "";
			//GetLicenseIssuerAddress(tx, strAddress);
			oLicense.push_back(Pair("value", value));
			//oLicense.push_back(Pair("txid", tx.GetHash().GetHex()));
			//oLicense.push_back(Pair("address", strAddress));
			oLicense.push_back(
					Pair("expires_in",
							nHeight + GetLicenseIssuerDisplayExpirationDepth(nHeight)
									- pindexBest->nHeight));
		}
		oRes.push_back(oLicense);
	}

	return oRes;
}



/*
 Value licenseclean(const Array& params, bool fHelp)
 {
 if (fHelp || params.size())
 throw runtime_error("license_clean\nClean unsatisfiable transactions from the wallet - including license_update on an already taken license\n");


 {
 LOCK2(cs_main,pwalletMain->cs_wallet);
 map<uint256, CWalletTx> mapRemove;

 printf("-----------------------------\n");

 {
 BOOST_FOREACH(PAIRTYPE(const uint256, CWalletTx)& item, pwalletMain->mapWallet)
 {
 CWalletTx& wtx = item.second;
 vector<unsigned char> vchLicenseIssuer;
 if (wtx.GetDepthInMainChain() < 1 && IsConflictedAliasTx(pblocktree, wtx, vchLicenseIssuer))
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
 vector<unsigned char> vchLicenseIssuer;
 if (GetNameOfLicenseTx(wtx, vchLicenseIssuer) && mapLicensePending.count(vchLicenseIssuer))
 {
 string license = stringFromVch(vchLicenseIssuer);
 printf("license_clean() : erase %s from pending of license %s",
 wtx.GetHash().GetHex().c_str(), license.c_str());
 if (!mapLicensePending[vchLicenseIssuer].erase(wtx.GetHash()))
 error("license_clean() : erase but it was not pending");
 }
 wtx.print();
 }

 printf("-----------------------------\n");
 }

 return true;
 }
 */
