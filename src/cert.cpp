#include "cert.h"
#include "init.h"
#include "txdb.h"
#include "util.h"
#include "auxpow.h"
#include "script.h"
#include "main.h"
#include "messagecrypter.h"
#include "json/json_spirit_reader_template.h"
#include "json/json_spirit_writer_template.h"
#include <boost/algorithm/hex.hpp>
#include <boost/xpressive/xpressive_dynamic.hpp>
using namespace std;
using namespace json_spirit;

template<typename T> void ConvertTo(Value& value, bool fAllowNull = false);


extern bool ExistsInMempool(std::vector<unsigned char> vchNameOrRand, opcodetype type);
extern bool HasReachedMainNetForkB2();
extern CCertDB *pcertdb;
extern COfferDB *pofferdb;
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
extern int CheckTransactionAtRelativeDepth(CBlockIndex* pindexBlock,
		const CCoins *txindex, int maxDepth);
extern int GetOfferExpirationDepth();
extern int GetTxPosHeight(const CDiskTxPos& txPos);
extern int GetTxPosHeight2(const CDiskTxPos& txPos, int nHeight);
extern int64 GetTxHashHeight(const uint256 txHash);
extern string getCurrencyToSYSFromAlias(const vector<unsigned char> &vchCurrency, int64 &nFee, const unsigned int &nHeightToFind, vector<string>& rateList, int &precision);
bool EncryptMessage(const vector<unsigned char> &vchPubKey, const vector<unsigned char> &vchMessage, string &strCipherText)
{
	CMessageCrypter crypter;
	if(!crypter.Encrypt(stringFromVch(vchPubKey), stringFromVch(vchMessage), strCipherText))
		return false;

	return true;
}
bool DecryptMessage(const vector<unsigned char> &vchPubKey, const vector<unsigned char> &vchCipherText, string &strMessage)
{
	std::vector<unsigned char> vchPubKeyByte;
	CKey PrivateKey;
    boost::algorithm::unhex(vchPubKey.begin(), vchPubKey.end(), std::back_inserter(vchPubKeyByte));
	CPubKey PubKey(vchPubKeyByte);
	CKeyID pubKeyID = PubKey.GetID();
	if (!pwalletMain->GetKey(pubKeyID, PrivateKey))
        return false;
	CBitcoinSecret Secret(PrivateKey);
	PrivateKey = Secret.GetKey();
	std::vector<unsigned char> vchPrivateKey(PrivateKey.begin(), PrivateKey.end());
	CMessageCrypter crypter;
	if(!crypter.Decrypt(HexStr(vchPrivateKey), stringFromVch(vchCipherText), strMessage))
		return false;
	
	return true;
}
void PutToCertList(std::vector<CCert> &certList, CCert& index) {
	int i = certList.size() - 1;
	BOOST_REVERSE_FOREACH(CCert &o, certList) {
        if(index.nHeight != 0 && o.nHeight == index.nHeight) {
        	certList[i] = index;
            return;
        }
        else if(o.txHash != 0 && o.txHash == index.txHash) {
        	certList[i] = index;
            return;
        }
        i--;
	}
    certList.push_back(index);
}
bool IsCertOp(int op) {
    return op == OP_CERT_ACTIVATE
        || op == OP_CERT_UPDATE
        || op == OP_CERT_TRANSFER;
}

// 10080 blocks = 1 week
// certificate  expiration time is ~ 6 months or 26 weeks
// expiration blocks is 262080 (final)
// expiration starts at 87360, increases by 1 per block starting at
// block 174721 until block 349440
int64 GetCertNetworkFee(opcodetype seed, unsigned int nHeight) {

	int64 nFee = 0;
	int64 nRate = 0;
	const vector<unsigned char> &vchCurrency = vchFromString("USD");
	vector<string> rateList;
	int precision;
	if(getCurrencyToSYSFromAlias(vchCurrency, nRate, nHeight, rateList, precision) != "")
		{
		if(seed==OP_CERT_ACTIVATE) 
		{
			nFee = 150 * COIN;
		}
		else if(seed==OP_CERT_UPDATE) 
		{
			nFee = 100 * COIN;
		} 
		else if(seed==OP_CERT_TRANSFER) 
		{
			nFee = 25 * COIN;
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


// Increase expiration to 36000 gradually starting at block 24000.
// Use for validation purposes and pass the chain height.
int GetCertExpirationDepth() {
    return 525600;
}

// For display purposes, pass the name height.
int GetCertDisplayExpirationDepth() {
    return GetCertExpirationDepth();
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
    case OP_CERT_ACTIVATE:
        return "certactivate";
    case OP_CERT_UPDATE:
        return "certupdate";
    case OP_CERT_TRANSFER:
        return "certtransfer";
    default:
        return "<unknown cert op>";
    }
}

bool CCert::UnserializeFromTx(const CTransaction &tx) {
    try {
        CDataStream dsCert(vchFromString(DecodeBase64(stringFromVch(tx.data))), SER_NETWORK, PROTOCOL_VERSION);
        dsCert >> *this;
    } catch (std::exception &e) {
        return false;
    }
    return true;
}

string CCert::SerializeToString() {
    // serialize cert object
    CDataStream dsCert(SER_NETWORK, PROTOCOL_VERSION);
    dsCert << *this;
    vector<unsigned char> vchData(dsCert.begin(), dsCert.end());
    return EncodeBase64(vchData.data(), vchData.size());
}

//TODO implement
bool CCertDB::ScanCerts(const std::vector<unsigned char>& vchCert, unsigned int nMax,
        std::vector<std::pair<std::vector<unsigned char>, CCert> >& certScan) {

    leveldb::Iterator *pcursor = pcertdb->NewIterator();

    CDataStream ssKeySet(SER_DISK, CLIENT_VERSION);
    ssKeySet << make_pair(string("certi"), vchCert);
    string sType;
    pcursor->Seek(ssKeySet.str());

    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        try {
            leveldb::Slice slKey = pcursor->key();
            CDataStream ssKey(slKey.data(), slKey.data() + slKey.size(), SER_DISK, CLIENT_VERSION);

            ssKey >> sType;
            if(sType == "certi") {
                vector<unsigned char> vchCert;
                ssKey >> vchCert;
                leveldb::Slice slValue = pcursor->value();
                CDataStream ssValue(slValue.data(), slValue.data() + slValue.size(), SER_DISK, CLIENT_VERSION);
                vector<CCert> vtxPos;
                ssValue >> vtxPos;
                CCert txPos;
                if (!vtxPos.empty())
                    txPos = vtxPos.back();
                certScan.push_back(make_pair(vchCert, txPos));
            }
            if (certScan.size() >= nMax)
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
 * [CCertDB::ReconstructCertIndex description]
 * @param  pindexRescan [description]
 * @return              [description]
 */
bool CCertDB::ReconstructCertIndex(CBlockIndex *pindexRescan) {
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

            // decode the cert op, params, height
            bool o = DecodeCertTx(tx, op, nOut, vvchArgs, -1);
            if (!o || !IsCertOp(op)) continue;

            vector<unsigned char> vchCert = vvchArgs[0];

            // get the transaction
            if(!GetTransaction(tx.GetHash(), tx, txblkhash, true))
                continue;

            // attempt to read cert from txn
            CCert txCert;
            CCert txCA;
            if(!txCert.UnserializeFromTx(tx))
                return error("ReconstructCertIndex() : failed to unserialize cert from tx");

            // save serialized cert
            CCert serializedCert = txCert;

            // read cert from DB if it exists
            vector<CCert> vtxPos;
            if (ExistsCert(vchCert)) {
                if (!ReadCert(vchCert, vtxPos))
                    return error("ReconstructCertIndex() : failed to read cert from DB");
            }

            txCert.txHash = tx.GetHash();
            txCert.nHeight = nHeight;
            // txn-specific values to cert object
            txCert.vchRand = vvchArgs[0];
            PutToCertList(vtxPos, txCert);

            if (!WriteCert(vchCert, vtxPos))
                return error("ReconstructCertIndex() : failed to write to cert DB");

          
            printf( "RECONSTRUCT CERT: op=%s cert=%s title=%s hash=%s height=%d\n",
                    certFromOp(op).c_str(),
                    stringFromVch(vvchArgs[0]).c_str(),
                    stringFromVch(txCert.vchTitle).c_str(),
                    tx.GetHash().ToString().c_str(),
                    nHeight);
        }
        pindex = pindex->pnext;
        
    }
	Flush();
    }
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

int GetCertHeight(vector<unsigned char> vchCert) {
    vector<CCert> vtxPos;
    if (pcertdb->ExistsCert(vchCert)) {
        if (!pcertdb->ReadCert(vchCert, vtxPos))
            return error("GetCertHeight() : failed to read from cert DB");
        if (vtxPos.empty()) return -1;
        CCert& txPos = vtxPos.back();
        return txPos.nHeight;
    }
    return -1;
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
        case OP_CERT_ACTIVATE:
        case OP_CERT_UPDATE:
        case OP_CERT_TRANSFER:
            cert = vvchArgs[0];
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
    case OP_CERT_ACTIVATE:
        value = vvch[2];
        return true;
    case OP_CERT_UPDATE:
    case OP_CERT_TRANSFER:
        value = vvch[1];
        return true;
    default:
        return false;
    }
}

bool IsCertMine(const CTransaction& tx) {
    if (tx.nVersion != SYSCOIN_TX_VERSION)
        return false;

    vector<vector<unsigned char> > vvch;
    int op, nOut;

    bool good = DecodeCertTx(tx, op, nOut, vvch, -1);
    if (!good) 
        return false;
    
    if(!IsCertOp(op))
        return false;

    const CTxOut& txout = tx.vout[nOut];
    if (IsMyCert(tx, txout)) {
        printf("IsCertMine() : found my transaction %s nout %d\n",
                tx.GetHash().GetHex().c_str(), nOut);
        return true;
    }
    return false;
}

bool IsCertMine(const CTransaction& tx, const CTxOut& txout) {
    if (tx.nVersion != SYSCOIN_TX_VERSION)
        return false;

    vector<vector<unsigned char> > vvch;
    int op, nOut;

    bool good = DecodeCertTx(tx, op, nOut, vvch, -1);
    if (!good) {
        return false;
    }
    if(!IsCertOp(op))
        return false;


    if (IsMyCert(tx, txout)) {
        printf("IsCertMine() : found my transaction %s value %d\n",
                tx.GetHash().GetHex().c_str(), (int) txout.nValue);
        return true;
    }
    return false;
}

bool GetValueOfCertTxHash(const uint256 &txHash,
        vector<unsigned char>& vchValue, uint256& hash, int& nHeight) {
    nHeight = GetTxHashHeight(txHash);
    CTransaction tx;
    uint256 blockHash;
    if (!GetTransaction(txHash, tx, blockHash, true))
        return error("GetValueOfCertTxHash() : could not read tx from disk");
    if (!GetValueOfCertTx(tx, vchValue))
        return error("GetValueOfCertTxHash() : could not decode value from tx");
    hash = tx.GetHash();
    return true;
}

bool GetValueOfCert(CCertDB& dbCert, const vector<unsigned char> &vchCert,
        vector<unsigned char>& vchValue, int& nHeight) {
    vector<CCert> vtxPos;
    if (!pcertdb->ReadCert(vchCert, vtxPos) || vtxPos.empty())
        return false;

    CCert& txPos = vtxPos.back();
    nHeight = txPos.nHeight;
    vchValue = txPos.vchRand;
    return true;
}

bool GetTxOfCert(CCertDB& dbCert, const vector<unsigned char> &vchCert,
        CCert& txPos, CTransaction& tx) {
    vector<CCert> vtxPos;
    if (!pcertdb->ReadCert(vchCert, vtxPos) || vtxPos.empty())
        return false;
    txPos = vtxPos.back();
    int nHeight = txPos.nHeight;
    if (nHeight + GetCertExpirationDepth()
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

bool DecodeCertTxInputs(const CTransaction& tx, int& op, int& nOut,
        vector<vector<unsigned char> >& vvch, CCoinsViewCache &inputs) {
    bool found = false;

	const COutPoint *prevOutput = NULL;
	const CCoins *prevCoins = NULL;
    // Strict check - bug disallowed
    for (unsigned int i = 0; i < tx.vin.size(); i++) {
        const CTxIn& in = tx.vin[i];
		prevOutput = &in.prevout;
		prevCoins = &inputs.GetCoins(prevOutput->hash);
        vector<vector<unsigned char> > vvchRead;
        if (DecodeCertScript(prevCoins->vout[prevOutput->n].scriptPubKey, op, vvchRead)) {
            nOut = i; found = true; vvch = vvchRead;
            break;
        }
    }
    if (!found) vvch.clear();
    return found && IsCertOp(op);
}
bool DecodeCertTx(const CTransaction& tx, int& op, int& nOut,
        vector<vector<unsigned char> >& vvch, int nHeight) {
    bool found = false;


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
    case OP_CERT_ACTIVATE:
        value = vvch[2];
        return true;
    case OP_CERT_UPDATE:
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

    if ((op == OP_CERT_ACTIVATE && vvch.size() == 3)
        || (op == OP_CERT_UPDATE && vvch.size() == 2)
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

    nFeeRet = nTransactionFee;
    while(true) {
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
    

    printf("CreateCertTransactionWithInputTx succeeded:\n%s",
            wtxNew.ToString().c_str());
    return true;
}
void EraseCert(CWalletTx& wtx)
{
	 UnspendInputs(wtx);
	 wtx.RemoveFromMemoryPool();
	 pwalletMain->EraseFromWallet(wtx.GetHash());
}
// nTxOut is the output from wtxIn that we should grab
string SendCertMoneyWithInputTx(vector<pair<CScript, int64> > &vecSend, int64 nValue,
        int64 nNetFee, CWalletTx& wtxIn, CWalletTx& wtxNew, bool fAskFee,
        const string& txData) {
    int nTxOut = IndexOfCertOutput(wtxIn);
    CReserveKey reservekey(pwalletMain);
    int64 nFeeRequired;

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
	{
        return _(
                "Error: The transaction was rejected.");
	}
    return "";
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
	{
        return _(
                "Error: The transaction was rejected.");
	}
    return "";
}

bool GetCertAddress(const CTransaction& tx, std::string& strAddress) {
    int op, nOut = 0;
    vector<vector<unsigned char> > vvch;

    if (!DecodeCertTx(tx, op, nOut, vvch, -1))
        return error("GetCertAddress() : could not decode cert tx.");

    const CTxOut& txout = tx.vout[nOut];

    const CScript& scriptPubKey = RemoveCertScriptPrefix(txout.scriptPubKey);
	CTxDestination dest;
	ExtractDestination(scriptPubKey, dest);
	strAddress = CBitcoinAddress(dest).ToString();
    return true;
}

bool GetCertAddress(const CDiskTxPos& txPos, std::string& strAddress) {
    CTransaction tx;
    if (!tx.ReadFromDisk(txPos))
        return error("GetCertAddress() : could not read tx from disk");
    return GetCertAddress(tx, strAddress);
}

CScript RemoveCertScriptPrefix(const CScript& scriptIn) {
    int op;
    vector<vector<unsigned char> > vvch;
    CScript::const_iterator pc = scriptIn.begin();

    if (!DecodeCertScript(scriptIn, op, vvch, pc))
        //throw runtime_error(
        //        "RemoveCertScriptPrefix() : could not decode cert script");
	printf ("RemoveCertScriptPrefix() : Could not decode cert script (softfail). This is is known to happen for some OPs annd prevents those from getting displayed or accounted for.");
    return CScript(pc, scriptIn.end());
}

bool CheckCertInputs(CBlockIndex *pindexBlock, const CTransaction &tx,
        CValidationState &state, CCoinsViewCache &inputs, bool fBlock, bool fMiner,
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
		vvchPrevArgs.clear();
        // Strict check - bug disallowed
		for (int i = 0; i < (int) tx.vin.size(); i++) {
			vector<vector<unsigned char> > vvch, vvch2;
			prevOutput = &tx.vin[i].prevout;
			prevCoins = &inputs.GetCoins(prevOutput->hash);
			if(DecodeCertScript(prevCoins->vout[prevOutput->n].scriptPubKey, prevOp, vvch))
			{
				vvchPrevArgs = vvch;
				found = true;
				break;
			}
			else if(DecodeOfferScript(prevCoins->vout[prevOutput->n].scriptPubKey, prevOp, vvch2))
			{
				found = true; 
				vvchPrevArgs = vvch2;
				break;
			}
			if(!found)vvchPrevArgs.clear();
			
		}
		
        // Make sure cert outputs are not spent by a regular transaction, or the cert would be lost
        if (tx.nVersion != SYSCOIN_TX_VERSION) {
            if (found)
                return error(
                        "CheckCertInputs() : a non-syscoin transaction with a syscoin input");
            return true;
        }
        vector<vector<unsigned char> > vvchArgs;
        int op, nOut;
        bool good = DecodeCertTx(tx, op, nOut, vvchArgs, -1);
        if (!good)
            return error("CheckCertInputs() : could not decode a syscoin tx");
        int nDepth;
        int64 nNetFee;
        // unserialize cert object from txn, check for valid
        CCert theCert;
        theCert.UnserializeFromTx(tx);
        if (theCert.IsNull())
            error("CheckCertInputs() : null cert object");
		if(theCert.vchData.size() > MAX_VALUE_LENGTH)
		{
			return error("cert data too big");
		}
		if(theCert.vchTitle.size() > MAX_NAME_LENGTH)
		{
			return error("cert title too big");
		}
		if(theCert.vchRand.size() > 20)
		{
			return error("cert rand too big");
		}
		if(theCert.vchPubKey.size() > MAX_VALUE_LENGTH)
		{
			return error("cert pub key too big");
		}
        if (vvchArgs[0].size() > MAX_NAME_LENGTH)
            return error("cert hex guid too long");
        switch (op) {
        case OP_CERT_ACTIVATE:
            if (found)
                return error(
                        "CheckCertInputs() : certactivate tx pointing to previous syscoin tx");
			if (vvchArgs[1].size() > 20)
				return error("cert tx with rand too big");
			if (vvchArgs[2].size() > MAX_NAME_LENGTH)
                return error("certactivate tx title too long");
			if (fBlock && !fJustCheck) {

					// check for enough fees
				nNetFee = GetCertNetFee(tx);
				if (nNetFee < GetCertNetworkFee(OP_CERT_ACTIVATE, theCert.nHeight))
					return error(
							"CheckCertInputs() : OP_CERT_ACTIVATE got tx %s with fee too low %lu",
							tx.GetHash().GetHex().c_str(),
							(long unsigned int) nNetFee);		
			}
            break;

        case OP_CERT_UPDATE:
			// if previous op was a cert or an offeraccept its ok
            if ( !found || (!IsCertOp(prevOp) && !IsOfferOp(prevOp)))
                return error("certupdate previous op is invalid");
            if (vvchPrevArgs[0] != vvchArgs[0] && !IsOfferOp(prevOp))
                return error("CheckCertInputs() : certupdate prev cert mismatch vvchPrevArgs[0]: %s, vvchArgs[0] %s", stringFromVch(vvchPrevArgs[0]).c_str(), stringFromVch(vvchArgs[0]).c_str());
            if (vvchArgs[1].size() > MAX_NAME_LENGTH)
                return error("certupdate tx title too long");

			if (fBlock && !fJustCheck) {
				if(vvchPrevArgs.size() > 0)
				{
					nDepth = CheckTransactionAtRelativeDepth(pindexBlock,
							prevCoins, GetOfferExpirationDepth());
					if ((fBlock || fMiner) && nDepth < 0)
						return error(
								"CheckCertInputs() : certupdate on an expired offer previous tx, or there is a pending transaction on the offer");		  
				}
				else
				{
					nDepth = CheckTransactionAtRelativeDepth(pindexBlock,
							prevCoins, GetCertExpirationDepth());
					if ((fBlock || fMiner) && nDepth < 0)
						return error(
								"CheckCertInputs() : certupdate on an expired cert, or there is a pending transaction on the cert");		  
				}
				// check for enough fees
				nNetFee = GetCertNetFee(tx);
				if (nNetFee < GetCertNetworkFee(OP_CERT_UPDATE, theCert.nHeight))
					return error(
							"CheckCertInputs() : OP_CERT_UPDATE got tx %s with fee too low %lu",
							tx.GetHash().GetHex().c_str(),
							(long unsigned int) nNetFee);
			}
            break;

        

        case OP_CERT_TRANSFER:
            // validate conditions
            if ( !found || !IsCertOp(prevOp))
                return error("certtransfer previous op is invalid");
        	if (vvchArgs[0].size() > 20)
				return error("certtransfer tx with cert rand too big");
            if (vvchArgs[1].size() > 20)
                return error("certtransfer tx with cert Cert hash too big");
            if (vvchPrevArgs[0] != vvchArgs[0])
                return error("CheckCertInputs() : certtransfer cert mismatch");
            if (fBlock && !fJustCheck) {
                // Check hash
                const vector<unsigned char> &vchCert = vvchArgs[0];

                // check for previous Cert
                nDepth = CheckTransactionAtRelativeDepth(pindexBlock,
                        prevCoins, GetCertExpirationDepth());
                if ((fBlock || fMiner) && nDepth < 0)
                    return error(
                            "CheckCertInputs() : certtransfer cannot be mined if Cert/certtransfer is not already in chain");

                // check for enough fees
                int64 expectedFee = GetCertNetworkFee(OP_CERT_TRANSFER, theCert.nHeight);
                nNetFee = GetCertNetFee(tx);
                if (nNetFee < expectedFee)
                    return error(
                            "CheckCertInputs() : OP_CERT_TRANSFER got tx %s with fee too low %lu",
                            tx.GetHash().GetHex().c_str(),
                            (long unsigned int) nNetFee);

                if(theCert.vchRand != vchCert)
                    return error("Cert txn contains invalid txnCert hash");


            }

            break;

        default:
            return error( "CheckCertInputs() : cert transaction has unknown op");
        }



        // these ifs are problably total bullshit except for the certnew
        if (fBlock || (!fBlock && !fMiner && !fJustCheck)) {
			// save serialized cert for later use
			CCert serializedCert = theCert;

			// if not an certnew, load the cert data from the DB
			vector<CCert> vtxPos;
			if (pcertdb->ExistsCert(vvchArgs[0]) && !fJustCheck) {
				if (!pcertdb->ReadCert(vvchArgs[0], vtxPos))
					return error(
							"CheckCertInputs() : failed to read from cert DB");
			}
            if (!fMiner && !fJustCheck && pindexBlock->nHeight != pindexBest->nHeight) {
                int nHeight = pindexBlock->nHeight;     
				
                // set the cert's txn-dependent values
				theCert.nHeight = pindexBlock->nHeight;
				theCert.txHash = tx.GetHash();
                theCert.vchRand = vvchArgs[0];
				PutToCertList(vtxPos, theCert);
				{
				TRY_LOCK(cs_main, cs_trymain);
                // write cert  
                if (!pcertdb->WriteCert(vvchArgs[0], vtxPos))
                    return error( "CheckCertInputs() : failed to write to cert DB");

              			
                // debug
                printf( "CONNECTED CERT: op=%s cert=%s title=%s hash=%s height=%d\n",
                        certFromOp(op).c_str(),
                        stringFromVch(vvchArgs[0]).c_str(),
                        stringFromVch(theCert.vchTitle).c_str(),
                        tx.GetHash().ToString().c_str(),
                        nHeight);
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

    strCert = stringFromVch(vvch[0]);

    address = strOp + ": " + strCert;
    return true;
}

void rescanforcerts(CBlockIndex *pindexRescan) {
    printf("Scanning blockchain for certs to create fast index...\n");
    pcertdb->ReconstructCertIndex(pindexRescan);
}


Value getcertfees(const Array& params, bool fHelp) {
    if (fHelp || 0 != params.size())
        throw runtime_error(
                "getaliasfees\n"
                        "get current service fees for alias transactions\n");
    Object oRes;
    oRes.push_back(Pair("height", nBestHeight ));
    oRes.push_back(Pair("activate_fee", ValueFromAmount(GetCertNetworkFee(OP_CERT_ACTIVATE, nBestHeight) )));
    oRes.push_back(Pair("update_fee", ValueFromAmount(GetCertNetworkFee(OP_CERT_UPDATE, nBestHeight) )));
    oRes.push_back(Pair("transfer_fee", ValueFromAmount(GetCertNetworkFee(OP_CERT_TRANSFER, nBestHeight) )));
    return oRes;

}

Value certnew(const Array& params, bool fHelp) {
    if (fHelp || params.size() < 2 || params.size() > 3)
        throw runtime_error(
		"certnew <title> <data> [private=0]\n"
                        "<title> title, 255 bytes max.\n"
                        "<data> data, 1KB max.\n"
						"<private> set to 1 if you only want to make the cert data private, only the owner of the cert can view it. Off by default.\n"
                        + HelpRequiringPassphrase());
	if(!HasReachedMainNetForkB2())
		throw runtime_error("Please wait until B2 hardfork starts in before executing this command.");
	vector<unsigned char> vchTitle = vchFromValue(params[0]);
    vector<unsigned char> vchData = vchFromValue(params[1]);
	bool bPrivate = false;
    if(vchTitle.size() < 1)
        throw runtime_error("certificate cannot be empty!");

    if(vchTitle.size() > MAX_NAME_LENGTH)
        throw runtime_error("certificate title cannot exceed 255 bytes!");

    if (vchData.size() > MAX_VALUE_LENGTH)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "certificate data cannot exceed 1023 bytes!");
	if(params.size() >= 3)
	{
		bPrivate = atoi(params[2].get_str().c_str()) == 1? true: false;
	}
    if (vchData.size() < 1)
	{
        vchData = vchFromString(" ");
		bPrivate = false;
	}
    // gather inputs
    uint64 rand = GetRand((uint64) -1);
    vector<unsigned char> vchRand = CBigNum(rand).getvch();
    vector<unsigned char> vchCert = vchFromValue(HexStr(vchRand));

    // this is a syscoin transaction
    CWalletTx wtx;
    wtx.nVersion = SYSCOIN_TX_VERSION;

	EnsureWalletIsUnlocked();
    //create certactivate txn keys
    CPubKey newDefaultKey;
    pwalletMain->GetKeyFromPool(newDefaultKey, false);
    CScript scriptPubKeyOrig;
    scriptPubKeyOrig.SetDestination(newDefaultKey.GetID());
    CScript scriptPubKey;

    
	// calculate network fees
	int64 nNetFee = GetCertNetworkFee(OP_CERT_ACTIVATE, nBestHeight);
	std::vector<unsigned char> vchPubKey(newDefaultKey.begin(), newDefaultKey.end());
	string strPubKey = HexStr(vchPubKey);
	if(bPrivate)
	{
		string strCipherText;
		if(!EncryptMessage(vchFromString(strPubKey), vchData, strCipherText))
		{
			throw JSONRPCError(RPC_WALLET_ERROR, "Could not encrypt certificate data!");
		}
		vchData = vchFromString(strCipherText);
	}
	// calculate net
    // build cert object
    CCert newCert;
    newCert.vchRand = vchCert;
    newCert.vchTitle = vchTitle;
	newCert.vchData = vchData;
	newCert.nHeight = nBestHeight;
	newCert.vchPubKey = vchFromString(strPubKey);
	newCert.bPrivate = bPrivate;
    string bdata = newCert.SerializeToString();

    scriptPubKey << CScript::EncodeOP_N(OP_CERT_ACTIVATE) << vchCert
            << vchRand << newCert.vchTitle << OP_2DROP << OP_2DROP;
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

	return wtx.GetHash().GetHex();
}

Value certupdate(const Array& params, bool fHelp) {
    if (fHelp || params.size() != 4)
        throw runtime_error(
		"certupdate <guid> <title> <data> <private>\n"
                        "Perform an update on an certificate you control.\n"
                        "<guid> certificate guidkey.\n"
                        "<title> certificate title, 255 bytes max.\n"
                        "<data> certificate data, 1KB max.\n"
						"<private> set to 1 if you only want to make the cert data private, only the owner of the cert can view it.\n"
                        + HelpRequiringPassphrase());
	if(!HasReachedMainNetForkB2())
		throw runtime_error("Please wait until B2 hardfork starts in before executing this command.");
    // gather & validate inputs
    vector<unsigned char> vchCert = vchFromValue(params[0]);
    vector<unsigned char> vchTitle = vchFromValue(params[1]);
    vector<unsigned char> vchData = vchFromValue(params[2]);
	bool bPrivate = atoi(params[3].get_str().c_str()) == 1? true: false;
    if(vchTitle.size() < 1)
        throw runtime_error("certificate title cannot be empty!");

    if(vchTitle.size() > MAX_NAME_LENGTH)
        throw runtime_error("certificate title cannot exceed 255 bytes!");

    if (vchData.size() < 1)
        vchData = vchFromString(" ");

    if (vchData.size() > MAX_VALUE_LENGTH)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "certificate data cannot exceed 1023 bytes!");

    // this is a syscoind txn
    CWalletTx wtx, wtxIn;
    wtx.nVersion = SYSCOIN_TX_VERSION;
    CScript scriptPubKeyOrig;


      	// check for existing cert 's
	if (ExistsInMempool(vchCert, OP_CERT_ACTIVATE) || ExistsInMempool(vchCert, OP_CERT_UPDATE) || ExistsInMempool(vchCert, OP_CERT_TRANSFER)) {
		throw runtime_error("there are pending operations on that cert");
	}
    EnsureWalletIsUnlocked();

    // look for a transaction with this key
    CTransaction tx;
	CCert cert;
    if (!GetTxOfCert(*pcertdb, vchCert, cert, tx))
        throw runtime_error("could not find a certificate with this key");

    // make sure cert is in wallet
	if (!pwalletMain->GetTransaction(tx.GetHash(), wtxIn) || !IsCertMine(tx)) 
		throw runtime_error("this cert is not in your wallet");
	
    // unserialize cert object from txn
    CCert theCert;
    if(!theCert.UnserializeFromTx(tx))
        throw runtime_error("cannot unserialize cert from txn");

    // get the cert from DB
    vector<CCert> vtxPos;
    if (!pcertdb->ReadCert(vchCert, vtxPos))
        throw runtime_error("could not read cert from DB");
    theCert = vtxPos.back();

    // get a key from our wallet set dest as ourselves
    CPubKey newDefaultKey;
    pwalletMain->GetKeyFromPool(newDefaultKey, false);
    scriptPubKeyOrig.SetDestination(newDefaultKey.GetID());

    // create CERTUPDATE txn keys
    CScript scriptPubKey;
    scriptPubKey << CScript::EncodeOP_N(OP_CERT_UPDATE) << vchCert << vchTitle
            << OP_2DROP << OP_DROP;
    scriptPubKey += scriptPubKeyOrig;
    // calculate network fees
    int64 nNetFee = GetCertNetworkFee(OP_CERT_UPDATE, nBestHeight);
	// if we want to make data private, encrypt it
	if(bPrivate)
	{
		string strCipherText;
		if(!EncryptMessage(theCert.vchPubKey, vchData, strCipherText))
		{
			throw JSONRPCError(RPC_WALLET_ERROR, "Could not encrypt certificate data!");
		}
		vchData = vchFromString(strCipherText);
	}


  
    theCert.vchRand = vchCert;
    theCert.vchTitle = vchTitle;
	theCert.vchData = vchData;
	theCert.nHeight = nBestHeight;
	theCert.bPrivate = bPrivate;



    // serialize cert object
    string bdata = theCert.SerializeToString();

    string strError = SendCertMoneyWithInputTx(scriptPubKey, MIN_AMOUNT, nNetFee,
            wtxIn, wtx, false, bdata);
    if (strError != "")
        throw JSONRPCError(RPC_WALLET_ERROR, strError);
    
    return wtx.GetHash().GetHex();
}


Value certtransfer(const Array& params, bool fHelp) {
 if (fHelp || params.size() != 2)
        throw runtime_error(
		"certtransfer <certkey> <pubkey>\n"
                "<certkey> certificate guidkey.\n"
				"<pubkey> Public key of the address you wish to transfer this certificate to.\n"
                 + HelpRequiringPassphrase());

	if(!HasReachedMainNetForkB2())
		throw runtime_error("Please wait until B2 hardfork starts in before executing this command.");
    // gather & validate inputs
    vector<unsigned char> vchCertKey = ParseHex(params[0].get_str());
	vector<unsigned char> vchCert = vchFromValue(params[0]);
	vector<unsigned char> vchPubKey = vchFromValue(params[1]);


	std::vector<unsigned char> vchPubKeyByte;
	CBitcoinAddress sendAddr;
	try
	{
		boost::algorithm::unhex(vchPubKey.begin(), vchPubKey.end(), std::back_inserter(vchPubKeyByte));
		CPubKey PubKey(vchPubKeyByte);
		CKeyID pubKeyID = PubKey.GetID();
		sendAddr = CBitcoinAddress(pubKeyID);
	}
	catch(std::exception &e)
	{
		throw JSONRPCError(RPC_WALLET_ERROR, "Public key is in a valid format!");
	}
	if(!sendAddr.IsValid())
		throw JSONRPCError(RPC_WALLET_ERROR, "Public key is invalid!");


    // this is a syscoin txn
    CWalletTx wtx, wtxIn;
    wtx.nVersion = SYSCOIN_TX_VERSION;
    CScript scriptPubKeyOrig;



    EnsureWalletIsUnlocked();

    // look for a transaction with this key, also returns
    // an cert object if it is found
    CTransaction tx;
    CCert theCert;
    if (!GetTxOfCert(*pcertdb, vchCert, theCert, tx))
        throw runtime_error("could not find a certificate with this key");

	// check to see if certificate in wallet
	if (!pwalletMain->GetTransaction(tx.GetHash(), wtxIn) || !IsCertMine(tx)) 
		throw runtime_error("this certificate is not in your wallet");

	if (ExistsInMempool(theCert.vchRand, OP_CERT_TRANSFER)) {
		throw runtime_error("there are pending operations on that cert ");
	}
	// unserialize cert object from txn
	if(!theCert.UnserializeFromTx(tx))
		throw runtime_error("cannot unserialize offer from txn");

	// get the cert from DB
	vector<CCert> vtxPos;
	if (!pcertdb->ReadCert(vchCert, vtxPos))
		throw runtime_error("could not read cert from DB");
	theCert = vtxPos.back();

    scriptPubKeyOrig.SetDestination(sendAddr.Get());
    CScript scriptPubKey;
    scriptPubKey << CScript::EncodeOP_N(OP_CERT_TRANSFER) << theCert.vchRand << vchCertKey << OP_2DROP << OP_DROP;
    scriptPubKey += scriptPubKeyOrig;

    // calculate network fees
    int64 nNetFee = GetCertNetworkFee(OP_CERT_TRANSFER, nBestHeight);
	// if cert is private, decrypt the data
	if(theCert.bPrivate)
	{
		string strData;
		string strCipherText;
		// decrypt using old key
		if(!DecryptMessage(theCert.vchPubKey, theCert.vchData, strData))
		{
			throw JSONRPCError(RPC_WALLET_ERROR, "Could not decrypt certificate data!");
		}
		// encrypt using new key
		if(!EncryptMessage(vchPubKey, vchFromString(strData), strCipherText))
		{
			throw JSONRPCError(RPC_WALLET_ERROR, "Could not encrypt certificate data!");
		}
		theCert.vchData = vchFromString(strCipherText);
		theCert.vchPubKey = vchPubKey;
	}	
	// check the offer links in the cert, can't xfer a cert thats linked to another offer
   if(!theCert.vchOfferLink.empty())
   {
		COffer myOffer;
		CTransaction txMyOffer;
		// if offer is still valid then we cannot xfer this cert
		if (GetTxOfOffer(*pofferdb, theCert.vchOfferLink, myOffer, txMyOffer))
		{
			string strError = strprintf("Cannot transfer this certificate, it is linked to offer: %s", stringFromVch(theCert.vchOfferLink).c_str());
			throw JSONRPCError(RPC_INVALID_PARAMETER, strError);
		}
   }
	theCert.nHeight = nBestHeight;
    // send the cert pay txn
    string strError = SendCertMoneyWithInputTx(scriptPubKey, MIN_AMOUNT, nNetFee,
            wtxIn, wtx, false, theCert.SerializeToString());
    if (strError != "")
        throw JSONRPCError(RPC_WALLET_ERROR, strError);
    
 
    // return results


	return wtx.GetHash().GetHex();
}


Value certinfo(const Array& params, bool fHelp) {
    if (fHelp || 1 != params.size())
        throw runtime_error("certinfo <guid>\n"
                "Show stored values of a single certificate and its .\n");

    vector<unsigned char> vchCert = vchFromValue(params[0]);

    // look for a transaction with this key, also returns
    // an cert object if it is found
    CTransaction tx;

	vector<CCert> vtxPos;

	int expired = 0;
	int expires_in = 0;
	int expired_block = 0;
    Object oCert;
    vector<unsigned char> vchValue;

	if (!pcertdb->ReadCert(vchCert, vtxPos))
		  throw JSONRPCError(RPC_WALLET_ERROR, "failed to read from cert DB");
	CCert ca = vtxPos.back();

    string sHeight = strprintf("%llu", ca.nHeight);
    oCert.push_back(Pair("cert", stringFromVch(vchCert)));
    oCert.push_back(Pair("txid", ca.txHash.GetHex()));
    oCert.push_back(Pair("height", sHeight));
    oCert.push_back(Pair("title", stringFromVch(ca.vchTitle)));


	string strData = stringFromVch(ca.vchData);
	string strDecrypted = "";
	if(ca.bPrivate)
	{
		strData = string("Encrypted for owner of certificate");
		if(DecryptMessage(ca.vchPubKey, ca.vchData, strDecrypted))
			strData = strDecrypted;
		
	}

    oCert.push_back(Pair("data", strData));
    oCert.push_back(Pair("is_mine", IsCertMine(tx) ? "true" : "false"));

    int nHeight;
    uint256 certHash;
    if (GetValueOfCertTxHash(ca.txHash, vchValue, certHash, nHeight)) {
        string strAddress = "";
        GetCertAddress(tx, strAddress);
        oCert.push_back(Pair("address", strAddress));
		expired_block = nHeight + GetCertDisplayExpirationDepth();
		if(nHeight + GetCertDisplayExpirationDepth() - pindexBest->nHeight <= 0)
		{
			expired = 1;
		}  
		if(expired == 0)
		{
			expires_in = nHeight + GetCertDisplayExpirationDepth() - pindexBest->nHeight;
		}
		oCert.push_back(Pair("expires_in", expires_in));
		oCert.push_back(Pair("expires_on", expired_block));
		oCert.push_back(Pair("expired", expired));
    
    }
    return oCert;
}

Value certlist(const Array& params, bool fHelp) {
    if (fHelp || 1 < params.size())
        throw runtime_error("certlist [<cert>]\n"
                "list my own Certificates");
	vector<unsigned char> vchName;

	if (params.size() == 1)
		vchName = vchFromValue(params[0]);
    vector<unsigned char> vchNameUniq;
    if (params.size() == 1)
        vchNameUniq = vchFromValue(params[0]);

    Array oRes;
    map< vector<unsigned char>, int > vNamesI;
    map< vector<unsigned char>, Object > vNamesO;

    uint256 blockHash;
    uint256 hash;
    CTransaction tx, dbtx;

    vector<unsigned char> vchValue;
    int nHeight;

    BOOST_FOREACH(PAIRTYPE(const uint256, CWalletTx)& item, pwalletMain->mapWallet)
    {
		int expired = 0;
		int expires_in = 0;
		int expired_block = 0;
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
		if (!DecodeCertTx(tx, op, nOut, vvch, -1) || !IsCertOp(op))
			continue;
		// get the txn height
		nHeight = GetTxHashHeight(hash);

		// get the txn cert name
		if (!GetNameOfCertTx(tx, vchName))
			continue;

		// skip this cert if it doesn't match the given filter value
		if (vchNameUniq.size() > 0 && vchNameUniq != vchName)
			continue;
		// get last active name only
		if (vNamesI.find(vchName) != vNamesI.end() && (nHeight < vNamesI[vchName] || vNamesI[vchName] < 0))
			continue;
		vector<CCert> vtxPos;
		if (!pcertdb->ReadCert(vchName, vtxPos))
			continue;
		CCert cert = vtxPos.back();
		if (!GetTransaction(cert.txHash, tx, blockHash, true))
			continue;
		if(!IsCertMine(tx))
			continue;
        // build the output object
        Object oName;
        oName.push_back(Pair("cert", stringFromVch(vchName)));
        oName.push_back(Pair("title", stringFromVch(cert.vchTitle)));

		string strData = stringFromVch(cert.vchData);
		string strDecrypted = "";
		if(cert.bPrivate)
		{
			strData = string("Encrypted for owner of certificate");
			if(DecryptMessage(cert.vchPubKey, cert.vchData, strDecrypted))
				strData = strDecrypted;
			
		}

		oName.push_back(Pair("data", strData));
        string strAddress = "";
        GetCertAddress(tx, strAddress);
        oName.push_back(Pair("address", strAddress));
		expired_block = nHeight + GetCertDisplayExpirationDepth();
		if(nHeight + GetCertDisplayExpirationDepth() - pindexBest->nHeight <= 0)
		{
			expired = 1;
		}  
		if(expired == 0)
		{
			expires_in = nHeight + GetCertDisplayExpirationDepth() - pindexBest->nHeight;
		}
		oName.push_back(Pair("expires_in", expires_in));
		oName.push_back(Pair("expires_on", expired_block));
		oName.push_back(Pair("expired", expired));
 
		vNamesI[vchName] = nHeight;
		vNamesO[vchName] = oName;	
    
	}
    BOOST_FOREACH(const PAIRTYPE(vector<unsigned char>, Object)& item, vNamesO)
        oRes.push_back(item.second);
    return oRes;
}


Value certhistory(const Array& params, bool fHelp) {
    if (fHelp || 1 != params.size())
        throw runtime_error("certhistory <cert>\n"
                "List all stored values of an cert.\n");

    Array oRes;
    vector<unsigned char> vchCert = vchFromValue(params[0]);
    string cert = stringFromVch(vchCert);

    {
        vector<CCert> vtxPos;
        if (!pcertdb->ReadCert(vchCert, vtxPos))
            throw JSONRPCError(RPC_WALLET_ERROR,
                    "failed to read from cert DB");

        CCert txPos2;
        uint256 txHash;
        uint256 blockHash;
        BOOST_FOREACH(txPos2, vtxPos) {
            txHash = txPos2.txHash;
            CTransaction tx;
			int expired = 0;
			int expires_in = 0;
			int expired_block = 0;
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
                GetCertAddress(tx, strAddress);
                oCert.push_back(Pair("address", strAddress));
				expired_block = nHeight + GetCertDisplayExpirationDepth();
				if(nHeight + GetCertDisplayExpirationDepth() - pindexBest->nHeight <= 0)
				{
					expired = 1;
				}  
				if(expired == 0)
				{
					expires_in = nHeight + GetCertDisplayExpirationDepth() - pindexBest->nHeight;
				}
				oCert.push_back(Pair("expires_in", expires_in));
				oCert.push_back(Pair("expires_on", expired_block));
				oCert.push_back(Pair("expired", expired));
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

    vector<unsigned char> vchCert;
    vector<pair<vector<unsigned char>, CCert> > certScan;
    if (!pcertdb->ScanCerts(vchCert, 100000000, certScan))
        throw JSONRPCError(RPC_WALLET_ERROR, "scan failed");

    pair<vector<unsigned char>, CCert> pairScan;
    BOOST_FOREACH(pairScan, certScan) {
		CCert txCert = pairScan.second;
        string cert = stringFromVch(txCert.vchRand);
		string certToSearch = cert;
		string title = stringFromVch(txCert.vchTitle);
		std::transform(title.begin(), title.end(), title.begin(), ::tolower);
		std::transform(certToSearch.begin(), certToSearch.end(), certToSearch.begin(), ::tolower);
		std::transform(strRegexp.begin(), strRegexp.end(), strRegexp.begin(), ::tolower);
        // regexp
        using namespace boost::xpressive;
        smatch certparts;
        sregex cregex = sregex::compile(strRegexp);
        if (strRegexp != "" && !regex_search(title, certparts, cregex) && strRegexp != certToSearch)
            continue;

        
        int nHeight = txCert.nHeight;

        // max age
        if (nMaxAge != 0 && pindexBest->nHeight - nHeight >= nMaxAge)
            continue;
        // from limits
        nCountFrom++;
        if (nCountFrom < nFrom + 1)
            continue;
        CTransaction tx;
        uint256 blockHash;
		if (!GetTransaction(txCert.txHash, tx, blockHash, true))
			continue;

		int expired = 0;
		int expires_in = 0;
		int expired_block = 0;
        Object oCert;
        oCert.push_back(Pair("cert", cert));
		vector<unsigned char> vchValue = txCert.vchTitle;
        string value = stringFromVch(vchValue);
        oCert.push_back(Pair("title", value));

		string strData = stringFromVch(txCert.vchData);
		string strDecrypted = "";
		if(txCert.bPrivate)
		{
			strData = string("Encrypted for owner of certificate");
			if(DecryptMessage(txCert.vchPubKey, txCert.vchData, strDecrypted))
				strData = strDecrypted;
			
		}

		oCert.push_back(Pair("data", strData));

		expired_block = nHeight + GetCertDisplayExpirationDepth();
		if(nHeight + GetCertDisplayExpirationDepth() - pindexBest->nHeight <= 0)
		{
			expired = 1;
		}  
		if(expired == 0)
		{
			expires_in = nHeight + GetCertDisplayExpirationDepth() - pindexBest->nHeight;
		}
		oCert.push_back(Pair("expires_in", expires_in));
		oCert.push_back(Pair("expires_on", expired_block));
		oCert.push_back(Pair("expired", expired));

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

    vector<unsigned char> vchCert;
    int nMax = 500;
    if (params.size() > 0) {
        vchCert = vchFromValue(params[0]);
    }

    if (params.size() > 1) {
        Value vMax = params[1];
        ConvertTo<double>(vMax);
        nMax = (int) vMax.get_real();
    }

    //CCertDB dbCert("r");
    Array oRes;

    vector<pair<vector<unsigned char>, CCert> > certScan;
    if (!pcertdb->ScanCerts(vchCert, nMax, certScan))
        throw JSONRPCError(RPC_WALLET_ERROR, "scan failed");

    pair<vector<unsigned char>, CCert> pairScan;
    BOOST_FOREACH(pairScan, certScan) {
        Object oCert;
        string cert = stringFromVch(pairScan.first);
        oCert.push_back(Pair("cert", cert));
        CTransaction tx;
        CCert txCert = pairScan.second;
        uint256 blockHash;
		int expired = 0;
		int expires_in = 0;
		int expired_block = 0;
        int nHeight = txCert.nHeight;
        vector<unsigned char> vchValue = txCert.vchTitle;
        string value = stringFromVch(vchValue);
		if (!GetTransaction(txCert.txHash, tx, blockHash, true))
			continue;

        //string strAddress = "";
        //GetCertAddress(tx, strAddress);
        oCert.push_back(Pair("value", value));
        //oCert.push_back(Pair("txid", tx.GetHash().GetHex()));
        //oCert.push_back(Pair("address", strAddress));
		expired_block = nHeight + GetCertDisplayExpirationDepth();
		if(nHeight + GetCertDisplayExpirationDepth() - pindexBest->nHeight <= 0)
		{
			expired = 1;
		}  
		if(expired == 0)
		{
			expires_in = nHeight + GetCertDisplayExpirationDepth() - pindexBest->nHeight;
		}
		oCert.push_back(Pair("expires_in", expires_in));
		oCert.push_back(Pair("expires_on", expired_block));
		oCert.push_back(Pair("expired", expired));
			
		oRes.push_back(oCert);
    }

    return oRes;
}



