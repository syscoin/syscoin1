#include "escrow.h"
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

std::map<std::vector<unsigned char>, uint256> mapMyEscrows;
std::map<std::vector<unsigned char>, uint256> mapMyEscrowItems;
std::map<std::vector<unsigned char>, std::set<uint256> > mapEscrowPending;
std::map<std::vector<unsigned char>, std::set<uint256> > mapEscrowItemPending;
std::list<CEscrowFee> lstEscrowFees;

#ifdef GUI
extern std::map<uint160, std::vector<unsigned char> > mapMyEscrowHashes;
#endif

extern CEscrowDB *pcertdb;

extern uint256 SignatureHash(CScript scriptCode, const CTransaction& txTo,
        unsigned int nIn, int nHashType);

CScript RemoveEscrowScriptPrefix(const CScript& scriptIn);
bool DecodeEscrowScript(const CScript& script, int& op,
        std::vector<std::vector<unsigned char> > &vvch,
        CScript::const_iterator& pc);

extern bool Solver(const CKeyStore& keystore, const CScript& scriptPubKey,
        uint256 hash, int nHashType, CScript& scriptSigRet,
        txnouttype& whichTypeRet);
extern bool VerifyScript(const CScript& scriptSig, const CScript& scriptPubKey,
        const CTransaction& txTo, unsigned int nIn, unsigned int flags,
        int nHashType);

bool IsEscrowOp(int op) {
    return op == OP_ESCROW_NEW
        || op == OP_ESCROW_ACTIVATE
        || op == OP_ESCROW_UPDATE
        || op == OP_ESCROWA_NEW
        || op == OP_ESCROWA_TRANSFER;
}

// 10080 blocks = 1 week
// certificate issuer expiration time is ~ 6 months or 26 weeks
// expiration blocks is 262080 (final)
// expiration starts at 87360, increases by 1 per block starting at
// block 174721 until block 349440

int nCStartHeight = 161280;
int64 GetEscrowNetworkFee(int seed, int nHeight) {
    int nComputedHeight = nHeight - nCStartHeight < 0 ? 1 : ( nHeight - nCStartHeight ) + 1;
    if (nComputedHeight >= 13440) nComputedHeight += (nComputedHeight - 13440) * 3;
    //if ((nComputedHeight >> 13) >= 60) return 0;
    int64 nStart = seed * COIN;
    if (fTestNet) nStart = 10 * CENT;
    else if(fCakeNet) return CENT;
    int64 nRes = nStart >> (nComputedHeight >> 13);
    nRes -= (nRes >> 14) * (nComputedHeight % 8192);
    nRes += CENT - 1;
    nRes = (nRes / CENT) * CENT;
    return nRes;
}

// Increase expiration to 36000 gradually starting at block 24000.
// Use for validation purposes and pass the chain height.
int GetEscrowExpirationDepth(int nHeight) {
    if (nHeight < 174720) return 87360;
    if (nHeight < 349440) return nHeight - 87360;
    return 262080;
}

// For display purposes, pass the name height.
int GetEscrowDisplayExpirationDepth(int nHeight) {
    return GetEscrowExpirationDepth(nHeight);
}

bool IsMyEscrow(const CTransaction& tx, const CTxOut& txout) {
    const CScript& scriptPubKey = RemoveEscrowScriptPrefix(txout.scriptPubKey);
    CScript scriptSig;
    txnouttype whichTypeRet;
    if (!Solver(*pwalletMain, scriptPubKey, 0, 0, scriptSig, whichTypeRet))
        return false;
    return true;
}

string certissuerFromOp(int op) {
    switch (op) {
    case OP_ESCROW_NEW:
        return "certissuernew";
    case OP_ESCROW_ACTIVATE:
        return "certissueractivate";
    case OP_ESCROW_UPDATE:
        return "certissuerupdate";
    case OP_ESCROWA_NEW:
        return "certnew";
    case OP_ESCROWA_TRANSFER:
        return "certtransfer";
    default:
        return "<unknown certissuer op>";
    }
}

bool CEscrow::UnserializeFromTx(const CTransaction &tx) {
    try {
        CDataStream dsEscrow(vchFromString(DecodeBase64(stringFromVch(tx.data))), SER_NETWORK, PROTOCOL_VERSION);
        dsEscrow >> *this;
    } catch (std::exception &e) {
        return false;
    }
    return true;
}

void CEscrow::SerializeToTx(CTransaction &tx) {
    vector<unsigned char> vchData = vchFromString(SerializeToString());
    tx.data = vchData;
}

string CEscrow::SerializeToString() {
    // serialize certissuer object
    CDataStream dsEscrow(SER_NETWORK, PROTOCOL_VERSION);
    dsEscrow << *this;
    vector<unsigned char> vchData(dsEscrow.begin(), dsEscrow.end());
    return EncodeBase64(vchData.data(), vchData.size());
}

//TODO implement
bool CEscrowDB::ScanEscrows(const std::vector<unsigned char>& vchEscrow, unsigned int nMax,
        std::vector<std::pair<std::vector<unsigned char>, CEscrow> >& certissuerScan) {

    leveldb::Iterator *pcursor = pcertdb->NewIterator();

    CDataStream ssKeySet(SER_DISK, CLIENT_VERSION);
    ssKeySet << make_pair(string("certissueri"), vchEscrow);
    string sType;
    pcursor->Seek(ssKeySet.str());

    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        try {
            leveldb::Slice slKey = pcursor->key();
            CDataStream ssKey(slKey.data(), slKey.data() + slKey.size(), SER_DISK, CLIENT_VERSION);

            ssKey >> sType;
            if(sType == "certissueri") {
                vector<unsigned char> vchEscrow;
                ssKey >> vchEscrow;
                leveldb::Slice slValue = pcursor->value();
                CDataStream ssValue(slValue.data(), slValue.data() + slValue.size(), SER_DISK, CLIENT_VERSION);
                vector<CEscrow> vtxPos;
                ssValue >> vtxPos;
                CEscrow txPos;
                if (!vtxPos.empty())
                    txPos = vtxPos.back();
                certissuerScan.push_back(make_pair(vchEscrow, txPos));
            }
            if (certissuerScan.size() >= nMax)
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
 * [CEscrowDB::ReconstructEscrowIndex description]
 * @param  pindexRescan [description]
 * @return              [description]
 */
bool CEscrowDB::ReconstructEscrowIndex(CBlockIndex *pindexRescan) {
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

            // decode the certissuer op, params, height
            bool o = DecodeEscrowTx(tx, op, nOut, vvchArgs, nHeight);
            if (!o || !IsEscrowOp(op)) continue;
            if (op == OP_ESCROW_NEW) continue;

            vector<unsigned char> vchEscrow = vvchArgs[0];

            // get the transaction
            if(!GetTransaction(tx.GetHash(), tx, txblkhash, true))
                continue;

            // attempt to read certissuer from txn
            CEscrow txEscrow;
            CEscrowItem txCA;
            if(!txEscrow.UnserializeFromTx(tx))
                return error("ReconstructEscrowIndex() : failed to unserialize certissuer from tx");

            // save serialized certissuer
            CEscrow serializedEscrow = txEscrow;

            // read certissuer from DB if it exists
            vector<CEscrow> vtxPos;
            if (ExistsEscrow(vchEscrow)) {
                if (!ReadEscrow(vchEscrow, vtxPos))
                    return error("ReconstructEscrowIndex() : failed to read certissuer from DB");
                if(vtxPos.size()!=0) {
                    txEscrow.nHeight = nHeight;
                    txEscrow.GetEscrowFromList(vtxPos);
                }
            }

            // read the certissuer certitem from db if exists
            if(op == OP_ESCROWA_NEW || op == OP_ESCROWA_TRANSFER) {
                bool bReadEscrow = false;
                vector<unsigned char> vchEscrowItem = vvchArgs[1];
                if (ExistsEscrowItem(vchEscrowItem)) {
                    if (!ReadEscrowItem(vchEscrowItem, vchEscrow))
                        printf("ReconstructEscrowIndex() : warning - failed to read certissuer certitem from certissuer DB\n");
                    else bReadEscrow = true;
                }
                if(!bReadEscrow && !txEscrow.GetEscrowItemByHash(vchEscrowItem, txCA))
                    printf("ReconstructEscrowIndex() : failed to read certissuer certitem from certissuer\n");

                // add txn-specific values to certissuer certitem object
                txCA.vchRand = vvchArgs[1];
                txCA.nTime = pindex->nTime;
                txCA.txHash = tx.GetHash();
                txCA.nHeight = nHeight;
                txEscrow.PutEscrowItem(txCA);
            }

            // use the txn certissuer as master on updates,
            // but grab the certitems from the DB first
            if(op == OP_ESCROW_UPDATE) {
                serializedEscrow.certs = txEscrow.certs;
                txEscrow = serializedEscrow;
            }

            if(op != OP_ESCROW_NEW) {
                // txn-specific values to certissuer object
                txEscrow.vchRand = vvchArgs[0];
                txEscrow.txHash = tx.GetHash();
                txEscrow.nHeight = nHeight;
                txEscrow.nTime = pindex->nTime;
                txEscrow.PutToEscrowList(vtxPos);

                if (!WriteEscrow(vchEscrow, vtxPos))
                    return error("ReconstructEscrowIndex() : failed to write to certissuer DB");
            }

            if(op == OP_ESCROWA_NEW || op == OP_ESCROWA_TRANSFER)
                if (!WriteEscrowItem(vvchArgs[1], vvchArgs[0]))
                    return error("ReconstructEscrowIndex() : failed to write to certissuer DB");

            // insert certissuers fees to regenerate list, write certissuer to
            // master index
            int64 nTheFee = GetEscrowNetFee(tx);
            InsertEscrowFee(pindex, tx.GetHash(), nTheFee);

            printf( "RECONSTRUCT ESCROWA: op=%s certissuer=%s title=%s hash=%s height=%d fees=%llu\n",
                    certissuerFromOp(op).c_str(),
                    stringFromVch(vvchArgs[0]).c_str(),
                    stringFromVch(txEscrow.vchTitle).c_str(),
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
int CheckEscrowTransactionAtRelativeDepth(CBlockIndex* pindexBlock,
        const CCoins *txindex, int maxDepth) {
    for (CBlockIndex* pindex = pindexBlock;
            pindex && pindexBlock->nHeight - pindex->nHeight < maxDepth;
            pindex = pindex->pprev)
        if (pindex->nHeight == (int) txindex->nHeight)
            return pindexBlock->nHeight - pindex->nHeight;
    return -1;
}

int GetEscrowTxHashHeight(const uint256 txHash) {
    CDiskTxPos postx;
    pblocktree->ReadTxIndex(txHash, postx);
    return postx.nPos;
}

uint64 GetEscrowFeeSubsidy(unsigned int nHeight) {

    unsigned int h12 = 360 * 12;
    unsigned int nTargetTime = 0;
    unsigned int nTarget1hrTime = 0;
    unsigned int blk1hrht = nHeight - 1;
    unsigned int blk12hrht = nHeight - 1;
    bool bFound = false;
    uint64 hr1 = 1, hr12 = 1;

    BOOST_FOREACH(CEscrowFee &nmFee, lstEscrowFees) {
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

bool InsertEscrowFee(CBlockIndex *pindex, uint256 hash, uint64 nValue) {
    unsigned int h12 = 3600 * 12;
    list<CEscrowFee> txnDup;
    CEscrowFee oFee;
    oFee.nTime = pindex->nTime;
    oFee.nHeight = pindex->nHeight;
    oFee.nFee = nValue;
    bool bFound = false;

    unsigned int tHeight =
            pindex->nHeight - 2880 < 0 ? 0 : pindex->nHeight - 2880;

    while (true) {
        if (lstEscrowFees.size() > 0
                && (lstEscrowFees.back().nTime + h12 < pindex->nTime
                        || lstEscrowFees.back().nHeight < tHeight))
            lstEscrowFees.pop_back();
        else
            break;
    }
    BOOST_FOREACH(CEscrowFee &nmFee, lstEscrowFees) {
        if (oFee.hash == nmFee.hash
                && oFee.nHeight == nmFee.nHeight) {
            bFound = true;
            break;
        }
    }
    if (!bFound)
        lstEscrowFees.push_front(oFee);

    return true;
}

int64 GetEscrowNetFee(const CTransaction& tx) {
    int64 nFee = 0;
    for (unsigned int i = 0; i < tx.vout.size(); i++) {
        const CTxOut& out = tx.vout[i];
        if (out.scriptPubKey.size() == 1 && out.scriptPubKey[0] == OP_RETURN)
            nFee += out.nValue;
    }
    return nFee;
}

int GetEscrowHeight(vector<unsigned char> vchEscrow) {
    vector<CEscrow> vtxPos;
    if (pcertdb->ExistsEscrow(vchEscrow)) {
        if (!pcertdb->ReadEscrow(vchEscrow, vtxPos))
            return error("GetEscrowHeight() : failed to read from certissuer DB");
        if (vtxPos.empty()) return -1;
        CEscrow& txPos = vtxPos.back();
        return txPos.nHeight;
    }
    return -1;
}

// Check that the last entry in certissuer history matches the given tx pos
bool CheckEscrowTxPos(const vector<CEscrow> &vtxPos, const int txPos) {
    if (vtxPos.empty()) return false;
    CEscrow certissuer;
    certissuer.nHeight = txPos;
    return certissuer.GetEscrowFromList(vtxPos);
}

int IndexOfEscrowOutput(const CTransaction& tx) {
    vector<vector<unsigned char> > vvch;
    int op, nOut;
    if (!DecodeEscrowTx(tx, op, nOut, vvch, -1))
        throw runtime_error("IndexOfEscrowOutput() : certissuer output not found");
    return nOut;
}

bool GetNameOfEscrowTx(const CTransaction& tx, vector<unsigned char>& certissuer) {
    if (tx.nVersion != SYSCOIN_TX_VERSION)
        return false;
    vector<vector<unsigned char> > vvchArgs;
    int op, nOut;
    if (!DecodeEscrowTx(tx, op, nOut, vvchArgs, -1))
        return error("GetNameOfEscrowTx() : could not decode a syscoin tx");

    switch (op) {
        case OP_ESCROWA_NEW:
        case OP_ESCROW_ACTIVATE:
        case OP_ESCROW_UPDATE:
        case OP_ESCROWA_TRANSFER:
            certissuer = vvchArgs[0];
            return true;
    }
    return false;
}

//TODO come back here check to see how / where this is used
bool IsConflictedEscrowTx(CBlockTreeDB& txdb, const CTransaction& tx,
        vector<unsigned char>& certissuer) {
    if (tx.nVersion != SYSCOIN_TX_VERSION)
        return false;
    vector<vector<unsigned char> > vvchArgs;
    int op, nOut, nPrevHeight;
    if (!DecodeEscrowTx(tx, op, nOut, vvchArgs, pindexBest->nHeight))
        return error("IsConflictedEscrowTx() : could not decode a syscoin tx");

    switch (op) {
    case OP_ESCROW_UPDATE:
        nPrevHeight = GetEscrowHeight(vvchArgs[0]);
        certissuer = vvchArgs[0];
        if (nPrevHeight >= 0
                && pindexBest->nHeight - nPrevHeight
                        < GetEscrowExpirationDepth(pindexBest->nHeight))
            return true;
    }
    return false;
}

bool GetValueOfEscrowTx(const CTransaction& tx, vector<unsigned char>& value) {
    vector<vector<unsigned char> > vvch;
    int op, nOut;

    if (!DecodeEscrowTx(tx, op, nOut, vvch, -1))
        return false;

    switch (op) {
    case OP_ESCROW_NEW:
        return false;
    case OP_ESCROW_ACTIVATE:
    case OP_ESCROWA_NEW:
        value = vvch[2];
        return true;
    case OP_ESCROW_UPDATE:
    case OP_ESCROWA_TRANSFER:
        value = vvch[1];
        return true;
    default:
        return false;
    }
}

bool IsEscrowMine(const CTransaction& tx) {
    if (tx.nVersion != SYSCOIN_TX_VERSION)
        return false;

    vector<vector<unsigned char> > vvch;
    int op, nOut;

    bool good = DecodeEscrowTx(tx, op, nOut, vvch, -1);
    if (!good) 
        return false;
    
    if(!IsEscrowOp(op))
        return false;

    const CTxOut& txout = tx.vout[nOut];
    if (IsMyEscrow(tx, txout)) {
        printf("IsEscrowMine() : found my transaction %s nout %d\n",
                tx.GetHash().GetHex().c_str(), nOut);
        return true;
    }
    return false;
}

bool IsEscrowMine(const CTransaction& tx, const CTxOut& txout,
        bool ignore_certissuernew) {
    if (tx.nVersion != SYSCOIN_TX_VERSION)
        return false;

    vector<vector<unsigned char> > vvch;
    int op, nOut;

    bool good = DecodeEscrowTx(tx, op, nOut, vvch, -1);
    if (!good) {
        error( "IsEscrowMine() : no output out script in cert tx %s\n",
                tx.ToString().c_str());
        return false;
    }
    if(!IsEscrowOp(op))
        return false;

    if (ignore_certissuernew && op == OP_ESCROW_NEW)
        return false;

    if (IsMyEscrow(tx, txout)) {
        printf("IsEscrowMine() : found my transaction %s value %d\n",
                tx.GetHash().GetHex().c_str(), (int) txout.nValue);
        return true;
    }
    return false;
}

bool GetValueOfEscrowTxHash(const uint256 &txHash,
        vector<unsigned char>& vchValue, uint256& hash, int& nHeight) {
    nHeight = GetEscrowTxHashHeight(txHash);
    CTransaction tx;
    uint256 blockHash;
    if (!GetTransaction(txHash, tx, blockHash, true))
        return error("GetValueOfEscrowTxHash() : could not read tx from disk");
    if (!GetValueOfEscrowTx(tx, vchValue))
        return error("GetValueOfEscrowTxHash() : could not decode value from tx");
    hash = tx.GetHash();
    return true;
}

bool GetValueOfEscrow(CEscrowDB& dbEscrow, const vector<unsigned char> &vchEscrow,
        vector<unsigned char>& vchValue, int& nHeight) {
    vector<CEscrow> vtxPos;
    if (!pcertdb->ReadEscrow(vchEscrow, vtxPos) || vtxPos.empty())
        return false;

    CEscrow& txPos = vtxPos.back();
    nHeight = txPos.nHeight;
    vchValue = txPos.vchRand;
    return true;
}

bool GetTxOfEscrow(CEscrowDB& dbEscrow, const vector<unsigned char> &vchEscrow,
        CTransaction& tx) {
    vector<CEscrow> vtxPos;
    if (!pcertdb->ReadEscrow(vchEscrow, vtxPos) || vtxPos.empty())
        return false;
    CEscrow& txPos = vtxPos.back();
    int nHeight = txPos.nHeight;
    if (nHeight + GetEscrowExpirationDepth(pindexBest->nHeight)
            < pindexBest->nHeight) {
        string certissuer = stringFromVch(vchEscrow);
        printf("GetTxOfEscrow(%s) : expired", certissuer.c_str());
        return false;
    }

    uint256 hashBlock;
    if (!GetTransaction(txPos.txHash, tx, hashBlock, true))
        return error("GetTxOfEscrow() : could not read tx from disk");

    return true;
}

bool GetTxOfEscrowItem(CEscrowDB& dbEscrow, const vector<unsigned char> &vchEscrowItem,
        CEscrow &txPos, CTransaction& tx) {
    vector<CEscrow> vtxPos;
    vector<unsigned char> vchEscrow;
    if (!pcertdb->ReadEscrowItem(vchEscrowItem, vchEscrow)) return false;
    if (!pcertdb->ReadEscrow(vchEscrow, vtxPos)) return false;
    txPos = vtxPos.back();
    int nHeight = txPos.nHeight;
    if (nHeight + GetEscrowExpirationDepth(pindexBest->nHeight)
            < pindexBest->nHeight) {
        string certissuer = stringFromVch(vchEscrowItem);
        printf("GetTxOfEscrowItem(%s) : expired", certissuer.c_str());
        return false;
    }

    uint256 hashBlock;
    if (!GetTransaction(txPos.txHash, tx, hashBlock, true))
        return error("GetTxOfEscrowItem() : could not read tx from disk");

    return true;
}

bool DecodeEscrowTx(const CTransaction& tx, int& op, int& nOut,
        vector<vector<unsigned char> >& vvch, int nHeight) {
    bool found = false;

    if (nHeight < 0)
        nHeight = pindexBest->nHeight;

    // Strict check - bug disallowed
    for (unsigned int i = 0; i < tx.vout.size(); i++) {
        const CTxOut& out = tx.vout[i];
        vector<vector<unsigned char> > vvchRead;
        if (DecodeEscrowScript(out.scriptPubKey, op, vvchRead)) {
            nOut = i; found = true; vvch = vvchRead;
            break;
        }
    }
    if (!found) vvch.clear();
    return found && IsEscrowOp(op);
}

bool GetValueOfEscrowTx(const CCoins& tx, vector<unsigned char>& value) {
    vector<vector<unsigned char> > vvch;

    int op, nOut;

    if (!DecodeEscrowTx(tx, op, nOut, vvch, -1))
        return false;

    switch (op) {
    case OP_ESCROW_NEW:
        return false;
    case OP_ESCROW_ACTIVATE:
    case OP_ESCROWA_NEW:
        value = vvch[2];
        return true;
    case OP_ESCROW_UPDATE:
    case OP_ESCROWA_TRANSFER:
        value = vvch[1];
        return true;
    default:
        return false;
    }
}

bool DecodeEscrowTx(const CCoins& tx, int& op, int& nOut,
        vector<vector<unsigned char> >& vvch, int nHeight) {
    bool found = false;

    if (nHeight < 0)
        nHeight = pindexBest->nHeight;

    // Strict check - bug disallowed
    for (unsigned int i = 0; i < tx.vout.size(); i++) {
        const CTxOut& out = tx.vout[i];
        vector<vector<unsigned char> > vvchRead;
        if (DecodeEscrowScript(out.scriptPubKey, op, vvchRead)) {
            nOut = i; found = true; vvch = vvchRead;
            break;
        }
    }
    if (!found)
        vvch.clear();
    return found;
}

bool DecodeEscrowScript(const CScript& script, int& op,
        vector<vector<unsigned char> > &vvch) {
    CScript::const_iterator pc = script.begin();
    return DecodeEscrowScript(script, op, vvch, pc);
}

bool DecodeEscrowScript(const CScript& script, int& op,
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

    if ((op == OP_ESCROW_NEW && vvch.size() == 1)
        || (op == OP_ESCROW_ACTIVATE && vvch.size() == 3)
        || (op == OP_ESCROW_UPDATE && vvch.size() == 2)
        || (op == OP_ESCROWA_NEW && vvch.size() == 3)
        || (op == OP_ESCROWA_TRANSFER && vvch.size() == 2))
        return true;
    return false;
}

bool SignEscrowSignature(const CTransaction& txFrom, CTransaction& txTo,
        unsigned int nIn, int nHashType = SIGHASH_ALL, CScript scriptPrereq =
                CScript()) {
    assert(nIn < txTo.vin.size());
    CTxIn& txin = txTo.vin[nIn];
    assert(txin.prevout.n < txFrom.vout.size());
    const CTxOut& txout = txFrom.vout[txin.prevout.n];

    // Leave out the signature from the hash, since a signature can't sign itself.
    // The checksig op will also drop the signatures from its hash.
    const CScript& scriptPubKey = RemoveEscrowScriptPrefix(txout.scriptPubKey);
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

bool CreateEscrowTransactionWithInputTx(
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
            printf("CreateEscrowTransactionWithInputTx: total value = %d\n",
                    (int) nTotalValue);
            double dPriority = 0;

            // vouts to the payees
            BOOST_FOREACH(const PAIRTYPE(CScript, int64)& s, vecSend)
                wtxNew.vout.push_back(CTxOut(s.second, s.first));

            int64 nWtxinCredit = wtxIn.vout[nTxOut].nValue;

            // Choose coins to use
            set<pair<const CWalletTx*, unsigned int> > setCoins;
            int64 nValueIn = 0;
            printf( "CreateEscrowTransactionWithInputTx: SelectCoins(%s), nTotalValue = %s, nWtxinCredit = %s\n",
                    FormatMoney(nTotalValue - nWtxinCredit).c_str(),
                    FormatMoney(nTotalValue).c_str(),
                    FormatMoney(nWtxinCredit).c_str());
            if (nTotalValue - nWtxinCredit > 0) {
                if (!pwalletMain->SelectCoins(nTotalValue - nWtxinCredit,
                        setCoins, nValueIn))
                    return false;
            }

            printf( "CreateEscrowTransactionWithInputTx: selected %d tx outs, nValueIn = %s\n",
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
                    if (!SignEscrowSignature(*coin.first, wtxNew, nIn++))
                        throw runtime_error("could not sign certissuer coin output");
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
                printf( "CreateEscrowTransactionWithInputTx: re-iterating (nFreeRet = %s)\n",
                        FormatMoney(nFeeRet).c_str());
                continue;
            }

            // Fill vtxPrev by copying from previous transactions vtxPrev
            wtxNew.AddSupportingTransactions();
            wtxNew.fTimeReceivedIsTxTime = true;

            break;
        }
    }

    printf("CreateEscrowTransactionWithInputTx succeeded:\n%s",
            wtxNew.ToString().c_str());
    return true;
}

// nTxOut is the output from wtxIn that we should grab
string SendEscrowMoneyWithInputTx(CScript scriptPubKey, int64 nValue,
        int64 nNetFee, CWalletTx& wtxIn, CWalletTx& wtxNew, bool fAskFee,
        const string& txData) {
    int nTxOut = IndexOfEscrowOutput(wtxIn);
    CReserveKey reservekey(pwalletMain);
    int64 nFeeRequired;
    vector<pair<CScript, int64> > vecSend;
    vecSend.push_back(make_pair(scriptPubKey, nValue));

    if (nNetFee) {
        CScript scriptFee;
        scriptFee << OP_RETURN;
        vecSend.push_back(make_pair(scriptFee, nNetFee));
    }

    if (!CreateEscrowTransactionWithInputTx(vecSend, wtxIn, nTxOut, wtxNew,
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

bool GetEscrowAddress(const CTransaction& tx, std::string& strAddress) {
    int op, nOut = 0;
    vector<vector<unsigned char> > vvch;

    if (!DecodeEscrowTx(tx, op, nOut, vvch, -1))
        return error("GetEscrowAddress() : could not decode certissuer tx.");

    const CTxOut& txout = tx.vout[nOut];

    const CScript& scriptPubKey = RemoveEscrowScriptPrefix(txout.scriptPubKey);
    strAddress = CBitcoinAddress(scriptPubKey.GetID()).ToString();
    return true;
}

bool GetEscrowAddress(const CDiskTxPos& txPos, std::string& strAddress) {
    CTransaction tx;
    if (!tx.ReadFromDisk(txPos))
        return error("GetEscrowAddress() : could not read tx from disk");
    return GetEscrowAddress(tx, strAddress);
}

CScript RemoveEscrowScriptPrefix(const CScript& scriptIn) {
    int op;
    vector<vector<unsigned char> > vvch;
    CScript::const_iterator pc = scriptIn.begin();

    if (!DecodeEscrowScript(scriptIn, op, vvch, pc))
        throw runtime_error(
                "RemoveEscrowScriptPrefix() : could not decode certissuer script");
    return CScript(pc, scriptIn.end());
}

bool CheckEscrowInputs(CBlockIndex *pindexBlock, const CTransaction &tx,
        CValidationState &state, CCoinsViewCache &inputs,
        map<vector<unsigned char>, uint256> &mapTestPool, bool fBlock, bool fMiner,
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
            if (DecodeEscrowScript(prevCoins->vout[prevOutput->n].scriptPubKey,
                    prevOp, vvch)) {
                found = true; vvchPrevArgs = vvch;
                break;
            }
            if(!found)vvchPrevArgs.clear();
        }

        // Make sure certissuer outputs are not spent by a regular transaction, or the certissuer would be lost
        if (tx.nVersion != SYSCOIN_TX_VERSION) {
            if (found)
                return error(
                        "CheckEscrowInputs() : a non-syscoin transaction with a syscoin input");
            return true;
        }

        vector<vector<unsigned char> > vvchArgs;
        int op;
        int nOut;
        bool good = DecodeEscrowTx(tx, op, nOut, vvchArgs, pindexBlock->nHeight);
        if (!good)
            return error("CheckEscrowInputs() : could not decode a syscoin tx");
        int nPrevHeight;
        int nDepth;
        int64 nNetFee;

        // unserialize certissuer object from txn, check for valid
        CEscrow theEscrow;
        CEscrowItem theEscrowItem;
        theEscrow.UnserializeFromTx(tx);
        if (theEscrow.IsNull())
            error("CheckEscrowInputs() : null certissuer object");

        if (vvchArgs[0].size() > MAX_NAME_LENGTH)
            return error("certissuer hex rand too long");

        switch (op) {
        case OP_ESCROW_NEW:

            if (found)
                return error(
                        "CheckEscrowInputs() : certissuernew tx pointing to previous syscoin tx");

            if (vvchArgs[0].size() != 20)
                return error("certissuernew tx with incorrect hash length");

            break;

        case OP_ESCROW_ACTIVATE:

            // check for enough fees
            nNetFee = GetEscrowNetFee(tx);
            if (nNetFee < GetEscrowNetworkFee(4, pindexBlock->nHeight) - COIN)
                return error(
                        "CheckEscrowInputs() : got tx %s with fee too low %lu",
                        tx.GetHash().GetHex().c_str(),
                        (long unsigned int) nNetFee);

            // validate conditions
            if ((!found || prevOp != OP_ESCROW_NEW) && !fJustCheck)
                return error("CheckEscrowInputs() : certissueractivate tx without previous certissuernew tx");

            if (vvchArgs[1].size() > 20)
                return error("certissueractivate tx with rand too big");

            if (vvchArgs[2].size() > MAX_VALUE_LENGTH)
                return error("certissueractivate tx with value too long");

            if (fBlock && !fJustCheck) {
                // Check hash
                const vector<unsigned char> &vchHash = vvchPrevArgs[0];
                const vector<unsigned char> &vchEscrow = vvchArgs[0];
                const vector<unsigned char> &vchRand = vvchArgs[1];
                vector<unsigned char> vchToHash(vchRand);
                vchToHash.insert(vchToHash.end(), vchEscrow.begin(), vchEscrow.end());
                uint160 hash = Hash160(vchToHash);

                if (uint160(vchHash) != hash)
                    return error(
                            "CheckEscrowInputs() : certissueractivate hash mismatch prev : %s cur %s",
                            HexStr(stringFromVch(vchHash)).c_str(), HexStr(stringFromVch(vchToHash)).c_str());

                // min activation depth is 1
                nDepth = CheckEscrowTransactionAtRelativeDepth(pindexBlock,
                        prevCoins, 1);
                if ((fBlock || fMiner) && nDepth >= 0 && (unsigned int) nDepth < 1)
                    return false;

                // check for previous certissuernew
                nDepth = CheckEscrowTransactionAtRelativeDepth(pindexBlock,
                        prevCoins,
                        GetEscrowExpirationDepth(pindexBlock->nHeight));
                if (nDepth == -1)
                    return error(
                            "CheckEscrowInputs() : certissueractivate cannot be mined if certissuernew is not already in chain and unexpired");

                nPrevHeight = GetEscrowHeight(vchEscrow);
                if (!fBlock && nPrevHeight >= 0
                        && pindexBlock->nHeight - nPrevHeight
                                < GetEscrowExpirationDepth(pindexBlock->nHeight))
                    return error(
                            "CheckEscrowInputs() : certissueractivate on an unexpired certissuer.");

                if(pindexBlock->nHeight == pindexBest->nHeight) {
                    BOOST_FOREACH(const MAPTESTPOOLTYPE& s, mapTestPool) {
                        if (vvchArgs[0] == s.first) {
                           return error("CheckInputs() : will not mine certissueractivate %s because it clashes with %s",
                                   tx.GetHash().GetHex().c_str(),
                                   s.second.GetHex().c_str());
                        }
                    }
                }
            }

            break;
        case OP_ESCROW_UPDATE:

            if (fBlock && fJustCheck && !found)
                return true;

            if ( !found || ( prevOp != OP_ESCROW_ACTIVATE && prevOp != OP_ESCROW_UPDATE 
                && prevOp != OP_ESCROWA_NEW  && prevOp != OP_ESCROWA_TRANSFER ) )
                return error("certissuerupdate previous op %s is invalid", certissuerFromOp(prevOp).c_str());

            if (vvchArgs[1].size() > MAX_VALUE_LENGTH)
                return error("certissuerupdate tx with value too long");

            if (vvchPrevArgs[0] != vvchArgs[0])
                return error("CheckEscrowInputs() : certissuerupdate certissuer mismatch");

            // TODO CPU intensive
            nDepth = CheckEscrowTransactionAtRelativeDepth(pindexBlock,
                    prevCoins, GetEscrowExpirationDepth(pindexBlock->nHeight));
            if ((fBlock || fMiner) && nDepth < 0)
                return error(
                        "CheckEscrowInputs() : certissuerupdate on an expired certissuer, or there is a pending transaction on the certissuer");

            if (fBlock && !fJustCheck && pindexBlock->nHeight == pindexBest->nHeight) {
                BOOST_FOREACH(const MAPTESTPOOLTYPE& s, mapTestPool) {
                    if (vvchArgs[0] == s.first) {
                       return error("CheckInputs() : will not mine certissuerupdate %s because it clashes with %s",
                               tx.GetHash().GetHex().c_str(),
                               s.second.GetHex().c_str());
                    }
                }
            }

            break;

        case OP_ESCROWA_NEW:

            if (vvchArgs[1].size() > 20)
                return error("certitem tx with rand too big");

            if (vvchArgs[2].size() > 20)
                return error("certitem tx with value too long");

            if (fBlock && !fJustCheck) {
                // Check hash
                const vector<unsigned char> &vchEscrow = vvchArgs[0];
                const vector<unsigned char> &vchEscrowItemRand = vvchArgs[1];

                nPrevHeight = GetEscrowHeight(vchEscrow);

                if(!theEscrow.GetEscrowItemByHash(vchEscrowItemRand, theEscrowItem))
                    return error("could not read certitem from certissuer txn");

                if(theEscrowItem.vchRand != vchEscrowItemRand)
                    return error("certitem txn contains invalid txncertitem hash");
            }
            break;

        case OP_ESCROWA_TRANSFER:

            // validate conditions
            if ( ( !found || prevOp != OP_ESCROWA_NEW ) && !fJustCheck )
                return error("certtransfer previous op %s is invalid", certissuerFromOp(prevOp).c_str());

            if (vvchArgs[0].size() > 20)
                return error("certtransfer tx with certissuer hash too big");

            if (vvchArgs[1].size() > 20)
                return error("certtransfer tx with certissuer certitem hash too big");

            if (fBlock && !fJustCheck) {
                // Check hash
                const vector<unsigned char> &vchEscrow = vvchArgs[0];
                const vector<unsigned char> &vchEscrowItem = vvchArgs[1];

                // construct certissuer certitem hash
                vector<unsigned char> vchToHash(vchEscrowItem);
                vchToHash.insert(vchToHash.end(), vchEscrow.begin(), vchEscrow.end());
                uint160 hash = Hash160(vchToHash);

                // check for previous certitem
                nDepth = CheckEscrowTransactionAtRelativeDepth(pindexBlock,
                        prevCoins, pindexBlock->nHeight);
                if (nDepth == -1)
                    return error(
                            "CheckEscrowInputs() : certtransfer cannot be mined if certitem is not already in chain");

                // check certissuer certitem hash against prev txn
                if (uint160(vvchPrevArgs[2]) != hash)
                    return error(
                            "CheckEscrowInputs() : certtransfer prev hash mismatch : %s vs %s",
                            HexStr(stringFromVch(vvchPrevArgs[2])).c_str(), HexStr(stringFromVch(vchToHash)).c_str());

                nPrevHeight = GetEscrowHeight(vchEscrow);

                if(!theEscrow.GetEscrowItemByHash(vchEscrowItem, theEscrowItem))
                    return error("could not read certitem from certissuer txn");

                // check for enough fees
                int64 expectedFee = GetEscrowNetworkFee(4, pindexBlock->nHeight) - COIN;
                nNetFee = GetEscrowNetFee(tx);
                if (nNetFee < expectedFee )
                    return error(
                            "CheckEscrowInputs() : got certtransfer tx %s with fee too low %lu",
                            tx.GetHash().GetHex().c_str(),
                            (long unsigned int) nNetFee);

                if(theEscrowItem.vchRand != vchEscrowItem)
                    return error("certitem txn contains invalid txncertitem hash");

                if(pindexBlock->nHeight == pindexBest->nHeight) {
                    BOOST_FOREACH(const MAPTESTPOOLTYPE& s, mapTestPool) {
                        if (vvchArgs[1] == s.first) {
                           return error("CheckInputs() : will not mine certtransfer %s because it clashes with %s",
                                   tx.GetHash().GetHex().c_str(),
                                   s.second.GetHex().c_str());
                        }
                    }
                }
            }

            break;

        default:
            return error( "CheckEscrowInputs() : certissuer transaction has unknown op");
        }

        // save serialized certissuer for later use
        CEscrow serializedEscrow = theEscrow;

        // if not an certissuernew, load the certissuer data from the DB
        vector<CEscrow> vtxPos;
        if(op != OP_ESCROW_NEW)
            if (pcertdb->ExistsEscrow(vvchArgs[0])) {
                if (!pcertdb->ReadEscrow(vvchArgs[0], vtxPos))
                    return error(
                            "CheckEscrowInputs() : failed to read from certissuer DB");
            }

//todo fucking suspect
        // // for certissuerupdate or certtransfer check to make sure the previous txn exists and is valid
        // if (!fBlock && fJustCheck && (op == OP_ESCROW_UPDATE || op == OP_ESCROWA_TRANSFER)) {
        // 	if (!CheckEscrowTxPos(vtxPos, prevCoins->nHeight))
        // 		return error(
        // 				"CheckEscrowInputs() : tx %s rejected, since previous tx (%s) is not in the certissuer DB\n",
        // 				tx.GetHash().ToString().c_str(),
        // 				prevOutput->hash.ToString().c_str());
        // }

        // these ifs are problably total bullshit except for the certissuernew
        if (fBlock || (!fBlock && !fMiner && !fJustCheck)) {
            if (op != OP_ESCROW_NEW) {
                if (!fMiner && !fJustCheck && pindexBlock->nHeight != pindexBest->nHeight) {
                    int nHeight = pindexBlock->nHeight;

                    // get the latest certissuer from the db
                    theEscrow.nHeight = nHeight;
                    theEscrow.GetEscrowFromList(vtxPos);

                    // If update, we make the serialized certissuer the master
                    // but first we assign the certitems from the DB since
                    // they are not shipped in an update txn to keep size down
                    if(op == OP_ESCROW_UPDATE) {
                        serializedEscrow.certs = theEscrow.certs;
                        theEscrow = serializedEscrow;
                    }

                    if (op == OP_ESCROWA_NEW || op == OP_ESCROWA_TRANSFER) {
                        // get the certitem out of the certissuer object in the txn
                        if(!serializedEscrow.GetEscrowItemByHash(vvchArgs[1], theEscrowItem))
                            return error("could not read certitem from certissuer txn");

                        // set the certissuer certitem txn-dependent values and add to the txn
                        theEscrowItem.vchRand = vvchArgs[1];
                        theEscrowItem.txHash = tx.GetHash();
                        theEscrowItem.nTime = pindexBlock->nTime;
                        theEscrowItem.nHeight = nHeight;
                        theEscrow.PutEscrowItem(theEscrowItem);

                        if (!pcertdb->WriteEscrowItem(vvchArgs[1], vvchArgs[0]))
                            return error( "CheckEscrowInputs() : failed to write to cert DB");
                        mapTestPool[vvchArgs[1]] = tx.GetHash();
                    }

                    if(op == OP_ESCROW_ACTIVATE || op == OP_ESCROW_UPDATE)
                        theEscrow.nHeight = pindexBlock->nHeight;

                    // set the certissuer's txn-dependent values
                    theEscrow.vchRand = vvchArgs[0];
                    theEscrow.txHash = tx.GetHash();
                    theEscrow.nTime = pindexBlock->nTime;
                    theEscrow.PutToEscrowList(vtxPos);

                    // write cert issuer 
                    if (!pcertdb->WriteEscrow(vvchArgs[0], vtxPos))
                        return error( "CheckEscrowInputs() : failed to write to cert DB");
                    mapTestPool[vvchArgs[0]] = tx.GetHash();
                    

                    // compute verify and write fee data to DB
                    int64 nTheFee = GetEscrowNetFee(tx);
                    InsertEscrowFee(pindexBlock, tx.GetHash(), nTheFee);
                    if(nTheFee > 0) printf("ESCROWA FEES: Added %lf in fees to track for regeneration.\n", (double) nTheFee / COIN);
                    vector<CEscrowFee> vEscrowFees(lstEscrowFees.begin(), lstEscrowFees.end());
                    if (!pcertdb->WriteEscrowFees(vEscrowFees))
                        return error( "CheckEscrowInputs() : failed to write fees to certissuer DB");

                    // remove certissuer from pendings

                    // activate or update - seller txn
                    if (op == OP_ESCROW_NEW || op == OP_ESCROW_ACTIVATE || op == OP_ESCROW_UPDATE) {
                        vector<unsigned char> vchEscrow = op == OP_ESCROW_NEW ?
                                    vchFromString(HexStr(vvchArgs[0])) : vvchArgs[0];
                        LOCK(cs_main);
                        std::map<std::vector<unsigned char>, std::set<uint256> >::iterator
                                mi = mapEscrowPending.find(vchEscrow);
                        if (mi != mapEscrowPending.end())
                            mi->second.erase(tx.GetHash());
                    }

                    // certitem or pay - buyer txn
                    else {
                        LOCK(cs_main);
                        std::map<std::vector<unsigned char>, std::set<uint256> >::iterator mi = mapEscrowItemPending.find(vvchArgs[1]);
                        if (mi != mapEscrowItemPending.end())
                            mi->second.erase(tx.GetHash());
                    }

                    // debug
                    printf( "CONNECTED ESCROWA: op=%s certissuer=%s title=%s hash=%s height=%d fees=%llu\n",
                            certissuerFromOp(op).c_str(),
                            stringFromVch(vvchArgs[0]).c_str(),
                            stringFromVch(theEscrow.vchTitle).c_str(),
                            tx.GetHash().ToString().c_str(),
                            nHeight, nTheFee / COIN);
                }
            }
        }
    }
    return true;
}

bool ExtractEscrowAddress(const CScript& script, string& address) {
    if (script.size() == 1 && script[0] == OP_RETURN) {
        address = string("network fee");
        return true;
    }
    vector<vector<unsigned char> > vvch;
    int op;
    if (!DecodeEscrowScript(script, op, vvch))
        return false;

    string strOp = certissuerFromOp(op);
    string strEscrow;
    if (op == OP_ESCROW_NEW) {
#ifdef GUI
        LOCK(cs_main);

        std::map<uint160, std::vector<unsigned char> >::const_iterator mi = mapMyEscrowHashes.find(uint160(vvch[0]));
        if (mi != mapMyEscrowHashes.end())
        strEscrow = stringFromVch(mi->second);
        else
#endif
        strEscrow = HexStr(vvch[0]);
    }
    else
        strEscrow = stringFromVch(vvch[0]);

    address = strOp + ": " + strEscrow;
    return true;
}

void rescanforcertissuers(CBlockIndex *pindexRescan) {
    printf("Scanning blockchain for certissuers to create fast index...\n");
    pcertdb->ReconstructEscrowIndex(pindexRescan);
}

int GetEscrowTxPosHeight(const CDiskTxPos& txPos) {
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

int GetEscrowTxPosHeight2(const CDiskTxPos& txPos, int nHeight) {
    nHeight = GetEscrowTxPosHeight(txPos);
    return nHeight;
}

Value certissuernew(const Array& params, bool fHelp) {
    if (fHelp || params.size() != 2)
        throw runtime_error(
                "certissuernew <title> <data>\n"
                        "<title> title, 255 bytes max."
                        "<data> data, 64KB max."
                        + HelpRequiringPassphrase());
    // gather inputs
    vector<unsigned char> vchTitle = vchFromValue(params[0]);
    vector<unsigned char> vchData = vchFromValue(params[1]);

    if(vchTitle.size() < 1)
        throw runtime_error("certificate title < 1 bytes!\n");

    if(vchTitle.size() > 255)
        throw runtime_error("certificate title > 255 bytes!\n");

    if (vchData.size() < 1)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "certificate data < 1 bytes!\n");

    if (vchData.size() > 64 * 1024)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "certificate data > 65536 bytes!\n");

    // set wallet tx ver
    CWalletTx wtx;
    wtx.nVersion = SYSCOIN_TX_VERSION;

    // generate rand identifier
    uint64 rand = GetRand((uint64) -1);
    vector<unsigned char> vchRand = CBigNum(rand).getvch();
    vector<unsigned char> vchEscrow = vchFromString(HexStr(vchRand));
    vector<unsigned char> vchToHash(vchRand);
    vchToHash.insert(vchToHash.end(), vchEscrow.begin(), vchEscrow.end());
    uint160 certissuerHash = Hash160(vchToHash);

    // build certissuer object
    CEscrow newEscrow;
    newEscrow.vchRand = vchEscrow;
    newEscrow.vchTitle = vchTitle;
    newEscrow.vchData = vchData;

    string bdata = newEscrow.SerializeToString();

    // create transaction keys
    CPubKey newDefaultKey;
    pwalletMain->GetKeyFromPool(newDefaultKey, false);
    CScript scriptPubKeyOrig;
    scriptPubKeyOrig.SetDestination(newDefaultKey.GetID());
    CScript scriptPubKey;
    scriptPubKey << CScript::EncodeOP_N(OP_ESCROW_NEW) << certissuerHash << OP_2DROP;
    scriptPubKey += scriptPubKeyOrig;

    // send transaction
    {
        LOCK(cs_main);
        EnsureWalletIsUnlocked();
        string strError = pwalletMain->SendMoney(scriptPubKey, MIN_AMOUNT, wtx,
                false, bdata);
        if (strError != "")
            throw JSONRPCError(RPC_WALLET_ERROR, strError);
        mapMyEscrows[vchEscrow] = wtx.GetHash();
    }
    printf("SENT:ESCROWANEW : title=%s, rand=%s, tx=%s, data:\n%s\n",
            stringFromVch(vchTitle).c_str(), stringFromVch(vchEscrow).c_str(),
            wtx.GetHash().GetHex().c_str(), bdata.c_str());

    // return results
    vector<Value> res;
    res.push_back(wtx.GetHash().GetHex());
    res.push_back(HexStr(vchRand));

    return res;
}

Value certissueractivate(const Array& params, bool fHelp) {
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
                "certissueractivate <rand> [<tx>]\n"
                        "Activate a certificate issuer after creating one with certissuernew.\n"
                        "<rand> certificate issuer randkey.\n"
                        + HelpRequiringPassphrase());

    // gather inputs
    vector<unsigned char> vchRand = ParseHex(params[0].get_str());
    vector<unsigned char> vchEscrow = vchFromValue(params[0]);

    // this is a syscoin transaction
    CWalletTx wtx;
    wtx.nVersion = SYSCOIN_TX_VERSION;

    // check for existing pending certissuers
    {
        LOCK2(cs_main, pwalletMain->cs_wallet);
        if (mapEscrowPending.count(vchEscrow)
                && mapEscrowPending[vchEscrow].size()) {
            error( "certissueractivate() : there are %d pending operations on that certificate issuer, including %s",
                   (int) mapEscrowPending[vchEscrow].size(),
                   mapEscrowPending[vchEscrow].begin()->GetHex().c_str());
            throw runtime_error("there are pending operations on that certissuer");
        }

        // look for an certificate issuer with identical hex rand keys. wont happen.
        CTransaction tx;
        if (GetTxOfEscrow(*pcertdb, vchEscrow, tx)) {
            error( "certissueractivate() : this certificate issuer is already active with tx %s",
                   tx.GetHash().GetHex().c_str());
            throw runtime_error("this certificate issuer is already active");
        }

        EnsureWalletIsUnlocked();

        // Make sure there is a previous certissuernew tx on this certificate issuer and that the random value matches
        uint256 wtxInHash;
        if (params.size() == 1) {
            if (!mapMyEscrows.count(vchEscrow))
                throw runtime_error(
                        "could not find a coin with this certissuer, try specifying the certissuernew transaction id");
            wtxInHash = mapMyEscrows[vchEscrow];
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
            if (DecodeEscrowScript(out.scriptPubKey, op, vvch)) {
                if (op != OP_ESCROW_NEW)
                    throw runtime_error(
                            "previous transaction wasn't a certissuernew");
                vchHash = vvch[0]; found = true;
                break;
            }
        }
        if (!found)
            throw runtime_error("Could not decode certissuer transaction");

        // calculate network fees
        int64 nNetFee = GetEscrowNetworkFee(4, pindexBest->nHeight);

        // unserialize certissuer object from txn, serialize back
        CEscrow newEscrow;
        if(!newEscrow.UnserializeFromTx(wtxIn))
            throw runtime_error(
                    "could not unserialize certissuer from txn");

        newEscrow.vchRand = vchEscrow;
        newEscrow.nFee = nNetFee;

        string bdata = newEscrow.SerializeToString();
        vector<unsigned char> vchbdata = vchFromString(bdata);

        // check this hash against previous, ensure they match
        vector<unsigned char> vchToHash(vchRand);
        vchToHash.insert(vchToHash.end(), vchEscrow.begin(), vchEscrow.end());
        uint160 hash = Hash160(vchToHash);
        if (uint160(vchHash) != hash)
            throw runtime_error("previous tx used a different random value");

        //create certissueractivate txn keys
        CPubKey newDefaultKey;
        pwalletMain->GetKeyFromPool(newDefaultKey, false);
        CScript scriptPubKeyOrig;
        scriptPubKeyOrig.SetDestination(newDefaultKey.GetID());
        CScript scriptPubKey;
        scriptPubKey << CScript::EncodeOP_N(OP_ESCROW_ACTIVATE) << vchEscrow
                << vchRand << newEscrow.vchTitle << OP_2DROP << OP_2DROP;
        scriptPubKey += scriptPubKeyOrig;

        // send the tranasction
        string strError = SendEscrowMoneyWithInputTx(scriptPubKey, MIN_AMOUNT,
                nNetFee, wtxIn, wtx, false, bdata);
        if (strError != "")
            throw JSONRPCError(RPC_WALLET_ERROR, strError);

        printf("SENT:ESCROWAACTIVATE: title=%s, rand=%s, tx=%s, data:\n%s\n",
                stringFromVch(newEscrow.vchTitle).c_str(),
                stringFromVch(vchEscrow).c_str(), wtx.GetHash().GetHex().c_str(),
                stringFromVch(vchbdata).c_str() );
    }
    return wtx.GetHash().GetHex();
}

Value certissuerupdate(const Array& params, bool fHelp) {
    if (fHelp || params.size() != 3)
        throw runtime_error(
                "certissuerupdate <rand> <title> <data>\n"
                        "Perform an update on an certificate issuer you control.\n"
                        "<rand> certificate issuer randkey.\n"
                        "<title> certificate issuer title, 255 bytes max.\n"
                        "<data> certificate issuer data, 64 KB max.\n"
                        + HelpRequiringPassphrase());

    // gather & validate inputs
    vector<unsigned char> vchEscrow = vchFromValue(params[0]);
    vector<unsigned char> vchTitle = vchFromValue(params[1]);
    vector<unsigned char> vchData = vchFromValue(params[2]);

    if(vchTitle.size() < 1)
        throw runtime_error("certificate issuer title < 1 bytes!\n");

    if(vchTitle.size() > 255)
        throw runtime_error("certificate issuer title > 255 bytes!\n");

    if (vchData.size() < 1)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "certificate issuer data < 1 bytes!\n");

    if (vchData.size() > 64 * 1024)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "certificate issuer data > 65536 bytes!\n");

    // this is a syscoind txn
    CWalletTx wtx;
    wtx.nVersion = SYSCOIN_TX_VERSION;
    CScript scriptPubKeyOrig;

    // get a key from our wallet set dest as ourselves
    CPubKey newDefaultKey;
    pwalletMain->GetKeyFromPool(newDefaultKey, false);
    scriptPubKeyOrig.SetDestination(newDefaultKey.GetID());

    // create ESCROWAUPDATE txn keys
    CScript scriptPubKey;
    scriptPubKey << CScript::EncodeOP_N(OP_ESCROW_UPDATE) << vchEscrow << vchTitle
            << OP_2DROP << OP_DROP;
    scriptPubKey += scriptPubKeyOrig;

    {
        LOCK2(cs_main, pwalletMain->cs_wallet);

        if (mapEscrowPending.count(vchEscrow)
                && mapEscrowPending[vchEscrow].size())
            throw runtime_error("there are pending operations on that certificate issuer");

        EnsureWalletIsUnlocked();

        // look for a transaction with this key
        CTransaction tx;
        if (!GetTxOfEscrow(*pcertdb, vchEscrow, tx))
            throw runtime_error("could not find a certificate issuer with this key");

        // make sure certissuer is in wallet
        uint256 wtxInHash = tx.GetHash();
        if (!pwalletMain->mapWallet.count(wtxInHash))
            throw runtime_error("this certificate issuer is not in your wallet");

        // unserialize certissuer object from txn
        CEscrow theEscrow;
        if(!theEscrow.UnserializeFromTx(tx))
            throw runtime_error("cannot unserialize certissuer from txn");

        // get the certissuer from DB
        vector<CEscrow> vtxPos;
        if (!pcertdb->ReadEscrow(vchEscrow, vtxPos))
            throw runtime_error("could not read certissuer from DB");
        theEscrow = vtxPos.back();
        theEscrow.certs.clear();

        // calculate network fees
        int64 nNetFee = GetEscrowNetworkFee(4, pindexBest->nHeight);

        // update certissuer values
        theEscrow.vchTitle = vchTitle;
        theEscrow.vchData = vchData;
        theEscrow.nFee += nNetFee;

        // serialize certissuer object
        string bdata = theEscrow.SerializeToString();

        CWalletTx& wtxIn = pwalletMain->mapWallet[wtxInHash];
        string strError = SendEscrowMoneyWithInputTx(scriptPubKey, MIN_AMOUNT, nNetFee,
                wtxIn, wtx, false, bdata);
        if (strError != "")
            throw JSONRPCError(RPC_WALLET_ERROR, strError);
    }
    return wtx.GetHash().GetHex();
}

Value certnew(const Array& params, bool fHelp) {
    if (fHelp || params.size() != 4)
        throw runtime_error("certnew <issuerkey> <address> <title> <data>\n"
                "Issue a new certificate.\n" 
                "<issuerkey> certificate issuer randkey.\n"
                "<title> certificate title, 255 bytes max.\n"
                "<data> certificate data, 64 KB max.\n"
                + HelpRequiringPassphrase());

    vector<unsigned char> vchEscrow = vchFromValue(params[0]);
    vector<unsigned char> vchAddress = vchFromValue(params[1]);
    vector<unsigned char> vchTitle = vchFromValue(params[2]);
    vector<unsigned char> vchData = vchFromValue(params[3]);
    CBitcoinAddress sendAddr(stringFromVch(vchAddress));
    if(!sendAddr.IsValid())
        throw runtime_error("Invalid Syscoin address.");

    if(vchTitle.size() < 1)
        throw runtime_error("certificate title < 1 bytes!\n");

    if(vchTitle.size() > 255)
        throw runtime_error("certificate title > 255 bytes!\n");

    if (vchData.size() < 1)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "certificate data < 1 bytes!\n");

    if (vchData.size() > 64 * 1024)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "certificate data > 65536 bytes!\n");

    // this is a syscoin txn
    CWalletTx wtx;
    wtx.nVersion = SYSCOIN_TX_VERSION;

    // generate certissuer certitem identifier and hash
    uint64 rand = GetRand((uint64) -1);
    vector<unsigned char> vchEscrowItemRand = CBigNum(rand).getvch();
    vector<unsigned char> vchToHash(vchEscrowItemRand);
    vchToHash.insert(vchToHash.end(), vchEscrow.begin(), vchEscrow.end());
    uint160 certitemHash = Hash160(vchToHash);

    CScript scriptPubKeyOrig;
    scriptPubKeyOrig.SetDestination(sendAddr.Get());
    CScript scriptPubKey;
    scriptPubKey << CScript::EncodeOP_N(OP_ESCROWA_NEW)
            << vchEscrow << vchEscrowItemRand << certitemHash << OP_2DROP << OP_2DROP;
    scriptPubKey += scriptPubKeyOrig;
    {
        LOCK2(cs_main, pwalletMain->cs_wallet);

        if (mapEscrowPending.count(vchEscrow)
                && mapEscrowPending[vchEscrow].size()) {
            error(  "certnew() : there are %d pending operations on that certificate issuer, including %s",
                    (int) mapEscrowPending[vchEscrow].size(),
                    mapEscrowPending[vchEscrow].begin()->GetHex().c_str());
            throw runtime_error("there are pending operations on that certificate issuer");
        }

        EnsureWalletIsUnlocked();

        // look for a transaction with this key
        CTransaction tx;
        if (!GetTxOfEscrow(*pcertdb, vchEscrow, tx))
            throw runtime_error("could not find a certificate issuer with this identifier");

        // unserialize certissuer object from txn
        CEscrow theEscrow;
        if(!theEscrow.UnserializeFromTx(tx))
            throw runtime_error("could not unserialize certificate issuer from txn");

        // get the certissuer id from DB
        vector<CEscrow> vtxPos;
        if (!pcertdb->ReadEscrow(vchEscrow, vtxPos))
            throw runtime_error("could not read certificate issuer with this key from DB");
        theEscrow = vtxPos.back();

        // create certitem object
        CEscrowItem txEscrowItem;
        txEscrowItem.vchRand = vchEscrowItemRand;
        txEscrowItem.vchTitle = vchTitle;
        txEscrowItem.vchData = vchData;
        theEscrow.certs.clear();
        theEscrow.PutEscrowItem(txEscrowItem);

        // serialize certissuer object
        string bdata = theEscrow.SerializeToString();

        string strError = pwalletMain->SendMoney(scriptPubKey, MIN_AMOUNT, wtx,
                false, bdata);
        if (strError != "")
            throw JSONRPCError(RPC_WALLET_ERROR, strError);
        mapMyEscrowItems[vchEscrowItemRand] = wtx.GetHash();
    }
    // return results
    vector<Value> res;
    res.push_back(wtx.GetHash().GetHex());
    res.push_back(HexStr(vchEscrowItemRand));

    return res;
}

Value certtransfer(const Array& params, bool fHelp) {
    if (fHelp || 2 != params.size())
        throw runtime_error("certtransfer <certkey> <address>\n"
                "Transfer a certificate to a syscoin address.\n"
                "<certkey> certificate randkey.\n"
                "<address> receiver syscoin address.\n"
                + HelpRequiringPassphrase());

    // gather & validate inputs
    vector<unsigned char> vchEscrowKey = ParseHex(params[0].get_str());
    vector<unsigned char> vchAddress = vchFromValue(params[1]);
    CBitcoinAddress sendAddr(stringFromVch(vchAddress));
    if(!sendAddr.IsValid())
        throw runtime_error("Invalid Syscoin address.");

    // this is a syscoin txn
    CWalletTx wtx;
    wtx.nVersion = SYSCOIN_TX_VERSION;
    CScript scriptPubKeyOrig;

    {
    LOCK2(cs_main, pwalletMain->cs_wallet);

    if (mapEscrowItemPending.count(vchEscrowKey)
            && mapEscrowItemPending[vchEscrowKey].size())
        throw runtime_error( "certtransfer() : there are pending operations on that certificate" );

    EnsureWalletIsUnlocked();

    // look for a transaction with this key, also returns
    // an certissuer object if it is found
    CTransaction tx;
    CEscrow theEscrow;
    CEscrowItem theEscrowItem;
    if (!GetTxOfEscrowItem(*pcertdb, vchEscrowKey, theEscrow, tx))
        throw runtime_error("could not find a certificate with this key");

    // check to see if certificate in wallet
    uint256 wtxInHash = tx.GetHash();
    if (!pwalletMain->mapWallet.count(wtxInHash))
        throw runtime_error("certtransfer() : certificate is not in your wallet" );

    // check that prev txn contains certissuer
    if(!theEscrow.UnserializeFromTx(tx))
        throw runtime_error("could not unserialize certificate from txn");

    // get the certissuer certitem from certissuer
    if(!theEscrow.GetEscrowItemByHash(vchEscrowKey, theEscrowItem))
        throw runtime_error("could not find a certificate with this name");

    // get the certissuer id from DB
    vector<unsigned char> vchEscrow;
    vector<CEscrow> vtxPos;
    if (!pcertdb->ReadEscrowItem(vchEscrowKey, vchEscrow))
        throw runtime_error("could not read certificate from DB");
    if (!pcertdb->ReadEscrow(vchEscrow, vtxPos))
        throw runtime_error("could not read certificate issuer with this key from DB");

    // hashes should match
    if(vtxPos.back().vchRand != theEscrow.vchRand)
        throw runtime_error("certificate issuer hash mismatch.");

    // use the certissuer and certificate from the DB as basis
    theEscrow = vtxPos.back();
    if(!theEscrow.GetEscrowItemByHash(vchEscrowKey, theEscrowItem))
        throw runtime_error("could not find a certificate with this hash in DB");

    // get a key from our wallet set dest as ourselves
    CPubKey newDefaultKey;
    pwalletMain->GetKeyFromPool(newDefaultKey, false);
    scriptPubKeyOrig.SetDestination(sendAddr.Get());
    CScript scriptPubKey;
    scriptPubKey << CScript::EncodeOP_N(OP_ESCROWA_TRANSFER) << vchEscrow << vchEscrowKey << OP_2DROP << OP_DROP;
    scriptPubKey += scriptPubKeyOrig;

    // make sure wallet is unlocked
    if (pwalletMain->IsLocked()) throw JSONRPCError(RPC_WALLET_UNLOCK_NEEDED,
        "Error: Please enter the wallet passphrase with walletpassphrase first.");

    // calculate network fees
    int64 nNetFee = GetEscrowNetworkFee(4, pindexBest->nHeight);

    theEscrowItem.nFee += nNetFee;
    theEscrow.certs.clear();
    theEscrow.PutEscrowItem(theEscrowItem);

    // send the certissuer pay txn
    CWalletTx& wtxIn = pwalletMain->mapWallet[wtxInHash];
    string strError = SendEscrowMoneyWithInputTx(scriptPubKey, MIN_AMOUNT, nNetFee,
            wtxIn, wtx, false, theEscrow.SerializeToString());
    if (strError != "")
        throw JSONRPCError(RPC_WALLET_ERROR, strError);
    }
    mapMyEscrowItems[vchEscrowKey] = 0;

    // return results
    vector<Value> res;
    res.push_back(wtx.GetHash().GetHex());

    return res;
}

Value certissuerinfo(const Array& params, bool fHelp) {
    if (fHelp || 1 != params.size())
        throw runtime_error("certissuerinfo <rand>\n"
                "Show stored values of an certificate issuer.\n");

    Object oLastEscrow;
    vector<unsigned char> vchEscrow = vchFromValue(params[0]);
    string certissuer = stringFromVch(vchEscrow);
    {
        LOCK(pwalletMain->cs_wallet);

        vector<CEscrow> vtxPos;
        if (!pcertdb->ReadEscrow(vchEscrow, vtxPos))
            throw JSONRPCError(RPC_WALLET_ERROR,
                    "failed to read from certissuer DB");
        if (vtxPos.size() < 1)
            throw JSONRPCError(RPC_WALLET_ERROR, "no result returned");

        // get transaction pointed to by alias
        CTransaction tx;
        uint256 blockHash;
        uint256 txHash = vtxPos.back().txHash;
        if (!GetTransaction(txHash, tx, blockHash, true))
            throw JSONRPCError(RPC_WALLET_ERROR, "failed to read transaction from disk");

        CEscrow theEscrow = vtxPos.back();

        Object oEscrow;
        vector<unsigned char> vchValue;
        Array aoEscrowItems;
        for(unsigned int i=0;i<theEscrow.certs.size();i++) {
            CEscrowItem ca = theEscrow.certs[i];
            Object oEscrowItem;
            string sTime = strprintf("%llu", ca.nTime);
            string sHeight = strprintf("%llu", ca.nHeight);

            oEscrowItem.push_back(Pair("id", HexStr(ca.vchRand)));
            oEscrowItem.push_back(Pair("txid", ca.txHash.GetHex()));
            oEscrowItem.push_back(Pair("height", sHeight));
            oEscrowItem.push_back(Pair("time", sTime));
            oEscrowItem.push_back(Pair("fee", (double)ca.nFee / COIN));
            oEscrowItem.push_back(Pair("title", stringFromVch(ca.vchTitle)));
            oEscrowItem.push_back(Pair("data", stringFromVch(ca.vchData)));
            aoEscrowItems.push_back(oEscrowItem);
        }
        int nHeight;
        uint256 certissuerHash;
        if (GetValueOfEscrowTxHash(txHash, vchValue, certissuerHash, nHeight)) {
            oEscrow.push_back(Pair("id", certissuer));
            oEscrow.push_back(Pair("txid", tx.GetHash().GetHex()));
            string strAddress = "";
            GetEscrowAddress(tx, strAddress);
            oEscrow.push_back(Pair("address", strAddress));
            oEscrow.push_back(
                    Pair("expires_in",
                            nHeight + GetEscrowDisplayExpirationDepth(nHeight)
                                    - pindexBest->nHeight));
            if (nHeight + GetEscrowDisplayExpirationDepth(nHeight)
                    - pindexBest->nHeight <= 0) {
                oEscrow.push_back(Pair("expired", 1));
            }
            oEscrow.push_back(Pair("title", stringFromVch(theEscrow.vchTitle)));
            oEscrow.push_back(Pair("data", stringFromVch(theEscrow.vchData)));
            oEscrow.push_back(Pair("certificates", aoEscrowItems));
            oLastEscrow = oEscrow;
        }
    }
    return oLastEscrow;

}

Value certinfo(const Array& params, bool fHelp) {
    if (fHelp || 1 != params.size())
        throw runtime_error("certissuerinfo <rand>\n"
                "Show stored values of a single certificate and its issuer.\n");

    vector<unsigned char> vchEscrowRand = ParseHex(params[0].get_str());
    Object oEscrowItem;

    // look for a transaction with this key, also returns
    // an certissuer object if it is found
    CTransaction tx;
    CEscrow theEscrow;
    CEscrowItem theEscrowItem;
    if (!GetTxOfEscrowItem(*pcertdb, vchEscrowRand, theEscrow, tx))
        throw runtime_error("could not find a certificate with this key");
    uint256 txHash = tx.GetHash();

    {
        LOCK(pwalletMain->cs_wallet);

        if(!theEscrow.GetEscrowItemByHash(vchEscrowRand, theEscrowItem))
            throw runtime_error("could not find a certificate with this hash in DB");

        Object oEscrow;
        vector<unsigned char> vchValue;

        CEscrowItem ca = theEscrowItem;
        string sTime = strprintf("%llu", ca.nTime);
        string sHeight = strprintf("%llu", ca.nHeight);
        oEscrowItem.push_back(Pair("id", HexStr(ca.vchRand)));
        oEscrowItem.push_back(Pair("txid", ca.txHash.GetHex()));
        oEscrowItem.push_back(Pair("height", sHeight));
        oEscrowItem.push_back(Pair("time", sTime));
        oEscrowItem.push_back(Pair("fee", (double)ca.nFee / COIN));
        oEscrowItem.push_back(Pair("title", stringFromVch(ca.vchTitle)));
        oEscrowItem.push_back(Pair("data", stringFromVch(ca.vchData)));

        int nHeight;
        uint256 certissuerHash;
        if (GetValueOfEscrowTxHash(txHash, vchValue, certissuerHash, nHeight)) {
            oEscrow.push_back(Pair("id", stringFromVch(theEscrow.vchRand) ));
            oEscrow.push_back(Pair("txid", tx.GetHash().GetHex()));
            string strAddress = "";
            GetEscrowAddress(tx, strAddress);
            oEscrow.push_back(Pair("address", strAddress));
            oEscrow.push_back(
                    Pair("expires_in",
                            nHeight + GetEscrowDisplayExpirationDepth(nHeight)
                                    - pindexBest->nHeight));
            if (nHeight + GetEscrowDisplayExpirationDepth(nHeight)
                    - pindexBest->nHeight <= 0) {
                oEscrow.push_back(Pair("expired", 1));
            }
            oEscrow.push_back(Pair("title", stringFromVch(theEscrow.vchTitle)));
            oEscrow.push_back(Pair("data", stringFromVch(theEscrow.vchData)));
            oEscrowItem.push_back(Pair("issuer", oEscrow));
        }
    }
    return oEscrowItem;
}

Value certissuerlist(const Array& params, bool fHelp) {
    if (fHelp || 1 < params.size())
        throw runtime_error("certissuerlist [<certissuer>]\n"
                "list my own certificate issuers");

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
        LOCK(pwalletMain->cs_wallet);

        uint256 blockHash;
        uint256 hash;
        CTransaction tx;

        vector<unsigned char> vchValue;
        int nHeight;

        BOOST_FOREACH(PAIRTYPE(const uint256, CWalletTx)& item, pwalletMain->mapWallet)
        {
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
            if (!DecodeEscrowTx(tx, op, nOut, vvch, -1) || !IsEscrowOp(op)) 
                continue;

            if(op == OP_ESCROWA_NEW || op == OP_ESCROWA_TRANSFER)
                continue;

            // get the txn height
            nHeight = GetEscrowTxHashHeight(hash);

            // get the txn alias name
            if(!GetNameOfEscrowTx(tx, vchName))
                continue;

            // skip this alias if it doesn't match the given filter value
            if(vchNameUniq.size() > 0 && vchNameUniq != vchName)
                continue;

            // get the value of the alias txn
            if(!GetValueOfEscrowTx(tx, vchValue))
                continue;

            // build the output object
            Object oName;
            oName.push_back(Pair("name", stringFromVch(vchName)));
            oName.push_back(Pair("value", stringFromVch(vchValue)));
            
            string strAddress = "";
            GetEscrowAddress(tx, strAddress);
            oName.push_back(Pair("address", strAddress));
            oName.push_back(Pair("expires_in", nHeight + GetEscrowDisplayExpirationDepth(nHeight) - pindexBest->nHeight));
            
            if(nHeight + GetEscrowDisplayExpirationDepth(nHeight) - pindexBest->nHeight <= 0)
                oName.push_back(Pair("expired", 1));

            // get last active name only
            if(vNamesI.find(vchName) != vNamesI.end() && vNamesI[vchName] > nHeight)
                continue;

            vNamesI[vchName] = nHeight;
            vNamesO[vchName] = oName;
        }
    }

    BOOST_FOREACH(const PAIRTYPE(vector<unsigned char>, Object)& item, vNamesO)
        oRes.push_back(item.second);

    return oRes;
}

Value certissuerhistory(const Array& params, bool fHelp) {
    if (fHelp || 1 != params.size())
        throw runtime_error("certissuerhistory <certissuer>\n"
                "List all stored values of an certissuer.\n");

    Array oRes;
    vector<unsigned char> vchEscrow = vchFromValue(params[0]);
    string certissuer = stringFromVch(vchEscrow);

    {
        LOCK(pwalletMain->cs_wallet);

        vector<CEscrow> vtxPos;
        if (!pcertdb->ReadEscrow(vchEscrow, vtxPos))
            throw JSONRPCError(RPC_WALLET_ERROR,
                    "failed to read from certissuer DB");

        CEscrow txPos2;
        uint256 txHash;
        uint256 blockHash;
        BOOST_FOREACH(txPos2, vtxPos) {
            txHash = txPos2.txHash;
            CTransaction tx;
            if (!GetTransaction(txHash, tx, blockHash, true)) {
                error("could not read txpos");
                continue;
            }

            Object oEscrow;
            vector<unsigned char> vchValue;
            int nHeight;
            uint256 hash;
            if (GetValueOfEscrowTxHash(txHash, vchValue, hash, nHeight)) {
                oEscrow.push_back(Pair("certissuer", certissuer));
                string value = stringFromVch(vchValue);
                oEscrow.push_back(Pair("value", value));
                oEscrow.push_back(Pair("txid", tx.GetHash().GetHex()));
                string strAddress = "";
                GetEscrowAddress(tx, strAddress);
                oEscrow.push_back(Pair("address", strAddress));
                oEscrow.push_back(
                        Pair("expires_in",
                                nHeight + GetEscrowDisplayExpirationDepth(nHeight)
                                        - pindexBest->nHeight));
                if (nHeight + GetEscrowDisplayExpirationDepth(nHeight)
                        - pindexBest->nHeight <= 0) {
                    oEscrow.push_back(Pair("expired", 1));
                }
                oRes.push_back(oEscrow);
            }
        }
    }
    return oRes;
}

Value certissuerfilter(const Array& params, bool fHelp) {
    if (fHelp || params.size() > 5)
        throw runtime_error(
                "certissuerfilter [[[[[regexp] maxage=36000] from=0] nb=0] stat]\n"
                        "scan and filter certissueres\n"
                        "[regexp] : apply [regexp] on certissueres, empty means all certissueres\n"
                        "[maxage] : look in last [maxage] blocks\n"
                        "[from] : show results from number [from]\n"
                        "[nb] : show [nb] results, 0 means all\n"
                        "[stats] : show some stats instead of results\n"
                        "certissuerfilter \"\" 5 # list certissueres updated in last 5 blocks\n"
                        "certissuerfilter \"^certissuer\" # list all certissueres starting with \"certissuer\"\n"
                        "certissuerfilter 36000 0 0 stat # display stats (number of certissuers) on active certissueres\n");

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

    //CEscrowDB dbEscrow("r");
    Array oRes;

    vector<unsigned char> vchEscrow;
    vector<pair<vector<unsigned char>, CEscrow> > certissuerScan;
    if (!pcertdb->ScanEscrows(vchEscrow, 100000000, certissuerScan))
        throw JSONRPCError(RPC_WALLET_ERROR, "scan failed");

    pair<vector<unsigned char>, CEscrow> pairScan;
    BOOST_FOREACH(pairScan, certissuerScan) {
        string certissuer = stringFromVch(pairScan.first);

        // regexp
        using namespace boost::xpressive;
        smatch certissuerparts;
        sregex cregex = sregex::compile(strRegexp);
        if (strRegexp != "" && !regex_search(certissuer, certissuerparts, cregex))
            continue;

        CEscrow txEscrow = pairScan.second;
        int nHeight = txEscrow.nHeight;

        // max age
        if (nMaxAge != 0 && pindexBest->nHeight - nHeight >= nMaxAge)
            continue;

        // from limits
        nCountFrom++;
        if (nCountFrom < nFrom + 1)
            continue;

        Object oEscrow;
        oEscrow.push_back(Pair("certissuer", certissuer));
        CTransaction tx;
        uint256 blockHash;
        uint256 txHash = txEscrow.txHash;
        if ((nHeight + GetEscrowDisplayExpirationDepth(nHeight) - pindexBest->nHeight
                <= 0) || !GetTransaction(txHash, tx, blockHash, true)) {
            oEscrow.push_back(Pair("expired", 1));
        } else {
            vector<unsigned char> vchValue = txEscrow.vchTitle;
            string value = stringFromVch(vchValue);
            oEscrow.push_back(Pair("value", value));
            oEscrow.push_back(
                    Pair("expires_in",
                            nHeight + GetEscrowDisplayExpirationDepth(nHeight)
                                    - pindexBest->nHeight));
        }
        oRes.push_back(oEscrow);

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

Value certissuerscan(const Array& params, bool fHelp) {
    if (fHelp || 2 > params.size())
        throw runtime_error(
                "certissuerscan [<start-certissuer>] [<max-returned>]\n"
                        "scan all certissuers, starting at start-certissuer and returning a maximum number of entries (default 500)\n");

    vector<unsigned char> vchEscrow;
    int nMax = 500;
    if (params.size() > 0) {
        vchEscrow = vchFromValue(params[0]);
    }

    if (params.size() > 1) {
        Value vMax = params[1];
        ConvertTo<double>(vMax);
        nMax = (int) vMax.get_real();
    }

    //CEscrowDB dbEscrow("r");
    Array oRes;

    vector<pair<vector<unsigned char>, CEscrow> > certissuerScan;
    if (!pcertdb->ScanEscrows(vchEscrow, nMax, certissuerScan))
        throw JSONRPCError(RPC_WALLET_ERROR, "scan failed");

    pair<vector<unsigned char>, CEscrow> pairScan;
    BOOST_FOREACH(pairScan, certissuerScan) {
        Object oEscrow;
        string certissuer = stringFromVch(pairScan.first);
        oEscrow.push_back(Pair("certissuer", certissuer));
        CTransaction tx;
        CEscrow txEscrow = pairScan.second;
        uint256 blockHash;

        int nHeight = txEscrow.nHeight;
        vector<unsigned char> vchValue = txEscrow.vchTitle;
        if ((nHeight + GetEscrowDisplayExpirationDepth(nHeight) - pindexBest->nHeight
                <= 0) || !GetTransaction(txEscrow.txHash, tx, blockHash, true)) {
            oEscrow.push_back(Pair("expired", 1));
        } else {
            string value = stringFromVch(vchValue);
            //string strAddress = "";
            //GetEscrowAddress(tx, strAddress);
            oEscrow.push_back(Pair("value", value));
            //oEscrow.push_back(Pair("txid", tx.GetHash().GetHex()));
            //oEscrow.push_back(Pair("address", strAddress));
            oEscrow.push_back(
                    Pair("expires_in",
                            nHeight + GetEscrowDisplayExpirationDepth(nHeight)
                                    - pindexBest->nHeight));
        }
        oRes.push_back(oEscrow);
    }

    return oRes;
}



/*
 Value certissuerclean(const Array& params, bool fHelp)
 {
 if (fHelp || params.size())
 throw runtime_error("certissuer_clean\nClean unsatisfiable transactions from the wallet - including certissuer_update on an already taken certissuer\n");


 {
 LOCK2(cs_main,pwalletMain->cs_wallet);
 map<uint256, CWalletTx> mapRemove;

 printf("-----------------------------\n");

 {
 BOOST_FOREACH(PAIRTYPE(const uint256, CWalletTx)& item, pwalletMain->mapWallet)
 {
 CWalletTx& wtx = item.second;
 vector<unsigned char> vchEscrow;
 if (wtx.GetDepthInMainChain() < 1 && IsConflictedAliasTx(pblocktree, wtx, vchEscrow))
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
 vector<unsigned char> vchEscrow;
 if (GetNameOfEscrowTx(wtx, vchEscrow) && mapEscrowPending.count(vchEscrow))
 {
 string certissuer = stringFromVch(vchEscrow);
 printf("certissuer_clean() : erase %s from pending of certissuer %s",
 wtx.GetHash().GetHex().c_str(), certissuer.c_str());
 if (!mapEscrowPending[vchEscrow].erase(wtx.GetHash()))
 error("certissuer_clean() : erase but it was not pending");
 }
 wtx.print();
 }

 printf("-----------------------------\n");
 }

 return true;
 }
 */
