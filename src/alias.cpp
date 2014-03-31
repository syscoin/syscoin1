// Copyright (c) 2010-2011 Vincent Durham
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.
//

#include "init.h"
#include "alias.h"
#include "txdb.h"
#include "util.h"
#include "auxpow.h"
#include "script.h"
#include "main.h"

#include "json/json_spirit_reader_template.h"
#include "json/json_spirit_writer_template.h"

using namespace std;
using namespace json_spirit;

extern CNameDB *paliasdb;

map<vector<unsigned char>, uint256> mapMyAliases;
map<vector<unsigned char>, set<uint256> > mapAliasesPending;
vector<vector<unsigned char> > vecNameIndex;
list<CAliasFee> lstAliasFees;

#ifdef GUI
extern std::map<uint160, std::vector<unsigned char> > mapMyNameHashes;
#endif

template<typename T>void ConvertTo(Value& value, bool fAllowNull=false);

extern uint256 SignatureHash(CScript scriptCode, const CTransaction& txTo, unsigned int nIn, int nHashType);

//static const bool NAME_DEBUG = false;
//extern int64 AmountFromValue(const Value& value);
extern Object JSONRPCError(int code, const string& message);

/** Global variable that points to the active block tree (protected by cs_main) */
extern CBlockTreeDB *pblocktree;

extern CCriticalSection cs_mapTransactions;
extern map<uint256, CTransaction> mapTransactions;

// forward decls
extern bool DecodeAliasScript(const CScript& script, int& op, vector<vector<unsigned char> > &vvch, CScript::const_iterator& pc);
extern bool Solver(const CKeyStore& keystore, const CScript& scriptPubKey, uint256 hash, int nHashType, CScript& scriptSigRet, txnouttype& whichTypeRet);
extern bool VerifyScript(const CScript& scriptSig, const CScript& scriptPubKey, const CTransaction& txTo, unsigned int nIn, unsigned int flags, int nHashType);
extern bool IsConflictedAliasTx(CBlockTreeDB& txdb, const CTransaction& tx, vector<unsigned char>& name);
extern void rescanforaliases(CBlockIndex *pindexRescan);
extern void rescanforoffers(CBlockIndex *pindexRescan);
//extern Value sendtoaddress(const Array& params, bool fHelp);

CScript RemoveAliasScriptPrefix(const CScript& scriptIn);
int GetAliasExpirationDepth(int nHeight);
int64 GetAliasNetFee(const CTransaction& tx);
bool CheckAliasTxPos(const vector<CAliasIndex> &vtxPos, const int txPos);


bool IsAliasOp(int op) {
    return op == OP_ALIAS_NEW || op == OP_ALIAS_ACTIVATE || op == OP_ALIAS_UPDATE;
}

bool InsertAliasFee(CBlockIndex *pindex, uint256 hash, uint64 vValue) {
    unsigned int h12 = 3600 * 12;
    list<CAliasFee> txnDup;
    CAliasFee txnVal(hash, pindex->nTime, pindex->nHeight, vValue);
    bool bFound = false;
    unsigned int tHeight = pindex->nHeight - 2880 < 0 ? 0 : pindex->nHeight - 2880;
    while(true) {
    	if(lstAliasFees.size()>0 && (lstAliasFees.back().nBlockTime + h12 < pindex->nTime
    			|| lstAliasFees.back().nHeight < tHeight))
    		lstAliasFees.pop_back();
    	else break;
    }
	BOOST_FOREACH(CAliasFee &nmTxnValue, lstAliasFees) {
		if(txnVal.hash == nmTxnValue.hash && txnVal.nHeight == nmTxnValue.nHeight) {
			bFound = true;
			break;
		}
	}
    if(!bFound) lstAliasFees.push_front(txnVal);
    return true;
}


uint64 GetAliasFeeSubsidy(unsigned int nHeight) {
	unsigned int h12 = 60 * 60 * 12;
	unsigned int nTargetTime = 0;
	unsigned int nTarget1hrTime = 0;
	unsigned int blk1hrht = nHeight - 1,
			blk12hrht = nHeight - 1;
	bool bFound = false;
	uint64 hr1 = 1, hr12 = 1;

	BOOST_FOREACH(CAliasFee &nmTxnValue, lstAliasFees) {
		if(nmTxnValue.nHeight <= nHeight)
			bFound = true;
		if(bFound) {
			if(nTargetTime==0) {
				hr1 = hr12 = 0;
				nTargetTime = nmTxnValue.nBlockTime - h12;
				nTarget1hrTime = nmTxnValue.nBlockTime - (h12/12);
			}
			if(nmTxnValue.nBlockTime > nTargetTime) {
				hr12 += nmTxnValue.nValue;
				blk12hrht = nmTxnValue.nHeight;
				if(nmTxnValue.nBlockTime > nTarget1hrTime) {
					hr1 += nmTxnValue.nValue;
					blk1hrht = nmTxnValue.nHeight;
				}
			}
		}
	}
	hr12 /= (nHeight - blk12hrht) + 1;
	hr1 /= (nHeight - blk1hrht) + 1;
//	printf("GetAliasFeeSubsidy() : Alias fee mining reward for height %d: %llu\n", nHeight, nSubsidyOut);
	return ( hr12 + hr1 ) / 2;
}

bool IsMyAlias(const CTransaction& tx, const CTxOut& txout) {
    const CScript& scriptPubKey = RemoveAliasScriptPrefix(txout.scriptPubKey);
    CScript scriptSig;
    txnouttype whichTypeRet;
    if (!Solver(*pwalletMain, scriptPubKey, 0, 0, scriptSig, whichTypeRet))
        return false;
    return true;
}

string aliasFromOp(int op) {
    switch (op) {
        case OP_ALIAS_NEW:
            return "aliasnew";
        case OP_ALIAS_UPDATE:
            return "aliasupdate";
        case OP_ALIAS_ACTIVATE:
            return "aliasactivate";
        default:
            return "<unknown alias op>";
    }
}
// get the depth of transaction txnindex relative to block at index pIndexBlock, looking
// up to maxdepth. Return relative depth if found, or -1 if not found and maxdepth reached.
int CheckTransactionAtRelativeDepth(CBlockIndex* pindexBlock, const CCoins *txindex, int maxDepth)
{
    for (CBlockIndex* pindex = pindexBlock; pindex && pindexBlock->nHeight - pindex->nHeight < maxDepth; pindex = pindex->pprev)
        if (pindex->nHeight == (int) txindex->nHeight)
            return pindexBlock->nHeight - pindex->nHeight;
    return -1;
}

int64 GetAliasNetFee(const CTransaction& tx) {
    int64 nFee = 0;
    for (unsigned int i = 0 ; i < tx.vout.size() ; i++) {
        const CTxOut& out = tx.vout[i];
        if (out.scriptPubKey.size() == 1 && out.scriptPubKey[0] == OP_RETURN)
            nFee += out.nValue;
    }
    return nFee;
}

int GetAliasHeight(vector<unsigned char> vchName) {
    vector<CAliasIndex> vtxPos;
    if (paliasdb->ExistsName(vchName)) {
        if (!paliasdb->ReadName(vchName, vtxPos))
            return error("GetAliasHeight() : failed to read from alias DB");
        if (vtxPos.empty())
            return -1;
        CAliasIndex& txPos = vtxPos.back();
        return txPos.nHeight;
    }
    return -1;
}

// Check that the last entry in name history matches the given tx pos
bool CheckAliasTxPos(const vector<CAliasIndex> &vtxPos, const int txPos) {
    if (vtxPos.empty()) return false;
    return vtxPos.back().nHeight == txPos;
}

/**
 * [IsAliasMine description]
 * @param  tx [description]
 * @return    [description]
 */
bool IsAliasMine(const CTransaction& tx) {
    if (tx.nVersion != SYSCOIN_TX_VERSION)
        return false;

    vector<vector<unsigned char> > vvch;
    int op, nOut;

    if (!DecodeAliasTx(tx, op, nOut, vvch, -1) || !IsAliasOp(op)) {
        error("IsAliasMine() : no output out script in alias tx %s\n",
        		tx.ToString().c_str());
        return false;
    }

    const CTxOut& txout = tx.vout[nOut];
    if (IsMyAlias(tx, txout)) {
        printf("IsAliasMine() : found my transaction %s nout %d\n",
        		tx.GetHash().GetHex().c_str(), nOut);
        return true;
    }
    return false;
}

bool IsAliasMine(const CTransaction& tx, const CTxOut& txout, bool ignore_aliasnew /* = false*/) {
    if (tx.nVersion != SYSCOIN_TX_VERSION)
        return false;

    vector<vector<unsigned char> > vvch;

    int op;

    if (!DecodeAliasScript(txout.scriptPubKey, op, vvch) || !IsAliasOp(op))
        return false;
    
    if (ignore_aliasnew && op == OP_ALIAS_NEW)
        return false;

    if (IsMyAlias(tx, txout)) {
        printf("IsAliasMine()  : found my transaction %s value %d\n", tx.GetHash().GetHex().c_str(), (int)txout.nValue);
        return true;
    }
    return false;
}

bool CheckAliasInputs(CBlockIndex *pindexBlock, const CTransaction &tx,
    CValidationState &state, CCoinsViewCache &inputs, map<uint256,uint256> &mapTestPool,
                      bool fBlock, bool fMiner, bool fJustCheck) {

    if(!tx.IsCoinBase()) {

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
            if (DecodeAliasScript(prevCoins->vout[prevOutput->n].scriptPubKey, prevOp, vvch)) {
				found = true;
                vvchPrevArgs = vvch;
				break;
			}
		}

        // Make sure alias outputs are not spent by a regular transaction, or the alias would be lost
        if (tx.nVersion != SYSCOIN_TX_VERSION) {
            if (found) return error("CheckAliasInputs() : a non-syscoin transaction with a syscoin input");
            return true;
        }

        // decode alias info from transaction
        vector<vector<unsigned char> > vvchArgs;
        int op, nOut, nPrevHeight, nDepth;
        if (!DecodeAliasTx(tx, op, nOut, vvchArgs, pindexBlock->nHeight))
            return error("CheckAliasInputs() : could not decode syscoin alias info from tx %s",
                         tx.GetHash().GetHex().c_str());
        int64 nNetFee;

        printf("%s : name=%s, tx=%s\n",
                aliasFromOp(op).c_str(),
                stringFromVch(op==OP_ALIAS_NEW ? vchFromString(HexStr(vvchArgs[0])) : vvchArgs[0]).c_str(),
                tx.GetHash().GetHex().c_str());

        switch (op) {

            case OP_ALIAS_NEW:

                // validate inputs
                if (found)
                    return error("CheckAliasInputs() : aliasnew tx pointing to previous syscoin tx");
                if (vvchArgs[0].size() != 20)
                    return error("aliasnew tx with incorrect hash length");

                printf("CONNECTED ALIAS: name=%s  op=%s  hash=%s  height=%d\n",
                    HexStr(vvchArgs[0]).c_str(),
                    aliasFromOp(op).c_str(),
                    tx.GetHash().ToString().c_str(),
                    pindexBlock->nHeight);

                break;

            case OP_ALIAS_ACTIVATE:

                // verify enough fees with this txn
                nNetFee = GetAliasNetFee(tx);
                if (nNetFee < GetAliasNetworkFee(pindexBlock->nHeight))
                    return error("CheckAliasInputs() : got tx %s with fee too low %lu",
                        tx.GetHash().GetHex().c_str(), (long unsigned int) nNetFee);

                // veryify that prev txn is aliasnew
                if ((!found || prevOp != OP_ALIAS_NEW) && !fJustCheck)
                    return error("CheckAliasInputs() : aliasactivate tx without previous aliasnew tx");

                // verify rand and value lengths
                if (vvchArgs[1].size() > 20)
                    return error("aliasactivate tx with rand too big");
                if (vvchArgs[2].size() > MAX_VALUE_LENGTH)
                    return error("aliasactivate tx with value too long");

                if (fBlock && !fJustCheck) {
    				// Check hash
    				const vector<unsigned char> &vchHash = vvchPrevArgs[0];
    				const vector<unsigned char> &vchName = vvchArgs[0];
    				const vector<unsigned char> &vchRand = vvchArgs[1];
    				vector<unsigned char> vchToHash(vchRand);
    				vchToHash.insert(vchToHash.end(), vchName.begin(), vchName.end());
    				uint160 hash = Hash160(vchToHash);
    				if (uint160(vchHash) != hash)
    					return error("CheckAliasInputs() : aliasactivate hash mismatch");

    				nDepth = CheckTransactionAtRelativeDepth(pindexBlock, prevCoins, MIN_FIRSTUPDATE_DEPTH);
    				if ((fBlock || fMiner) && nDepth >= 0 && (unsigned int) nDepth < MIN_FIRSTUPDATE_DEPTH)
    					return false;
                    nDepth = CheckTransactionAtRelativeDepth(pindexBlock, prevCoins,
                                                             GetAliasExpirationDepth(pindexBlock->nHeight));
                    if (nDepth == -1)
                        return error("CheckAliasInputs() : aliasactivate cannot be mined if aliasnew is not already in chain and unexpired");

                    nPrevHeight = GetAliasHeight(vvchArgs[0]);
                    if (!fBlock && nPrevHeight >= 0 && pindexBlock->nHeight - nPrevHeight <
                            GetAliasExpirationDepth(pindexBlock->nHeight))
    					return error("CheckAliasInputs() : aliasactivate on an unexpired alias");

                    // set<uint256>& setPending = mapAliasesPending[vvchArgs[0]];
                    // BOOST_FOREACH(const PAIRTYPE(uint256, uint256)& s, mapTestPool) {
                    //     if (setPending.count(s.second)) {
                    //         printf("CheckAliasInputs() : will not mine %s because it clashes with %s",
                    //                tx.GetHash().GetHex().c_str(),
                    //                s.second.GetHex().c_str());
                    //         return false;
                    //     }
                    // }
                }

                break;

            case OP_ALIAS_UPDATE:

                if(fBlock && fJustCheck && !found) return true;

                if (!found || (prevOp != OP_ALIAS_ACTIVATE && prevOp != OP_ALIAS_UPDATE))
                    return error("aliasupdate tx without previous update tx");

                if (vvchArgs[1].size() > MAX_VALUE_LENGTH)
                    return error("aliasupdate tx with value too long");

                // Check name
                if (vvchPrevArgs[0] != vvchArgs[0])
                    return error("CheckAliasInputs() : aliasupdate alias mismatch");

                // TODO CPU intensive
                nDepth = CheckTransactionAtRelativeDepth(pindexBlock, prevCoins, 
                    GetAliasExpirationDepth(pindexBlock->nHeight));
                if ((fBlock || fMiner) && nDepth < 0)
                    return error("CheckAliasInputs() : aliasupdate on an expired alias, or there is a pending transaction on the alias");
                
                break;

            default:
                return error("CheckAliasInputs() : alias transaction has unknown op");
        }

        if (fBlock || (!fBlock && !fMiner && !fJustCheck)) {
            
            if (op != OP_ALIAS_NEW) {

                // get the alias from the DB
                vector<CAliasIndex> vtxPos;
				if (paliasdb->ExistsName(vvchArgs[0])) {
					if (!paliasdb->ReadName(vvchArgs[0], vtxPos) && op == OP_ALIAS_UPDATE && !fJustCheck)
						return error("CheckAliasInputs() : failed to read from alias DB");
				}

                // if an update then check for a prevtx and error out if not found
				if (fJustCheck && op == OP_ALIAS_UPDATE && !CheckAliasTxPos(vtxPos, prevCoins->nHeight)) {
					printf("CheckAliasInputs() : tx %s rejected, since previous tx (%s) is not in the alias DB\n",
						tx.GetHash().ToString().c_str(), prevOutput->hash.ToString().c_str());
					return false;
				}

                if(!fMiner && !fJustCheck && pindexBlock->nHeight != pindexBest->nHeight) {
                    
                    int nHeight = pindexBlock->nHeight;
                    vector<unsigned char> vchVal;
                    CAliasIndex txPos2;
                    uint256 hash;
                    GetValueOfNameTxHash(tx.GetHash(), vchVal, hash, nHeight);
                    txPos2.nHeight = nHeight;
                    txPos2.vValue = vchVal;
                    txPos2.txHash = tx.GetHash();
                    txPos2.txPrevOut = *prevOutput;

                    if(vtxPos.size()>0 && vtxPos.back().nHeight == nHeight)
                    	vtxPos.pop_back();
                    vtxPos.push_back(txPos2); // fin add

                    if (!paliasdb->WriteName(vvchArgs[0], vtxPos))
                        return error("CheckAliasInputs() : failed to write to alias DB");

                    // write alias fees to db
                    int64 nTheFee = GetAliasNetFee(tx);
                    InsertAliasFee(pindexBlock, tx.GetHash(), nTheFee);
                    if( nTheFee != 0) printf("ALIAS FEES: Added %lf in fees to track for regeneration.\n", 
                        (double) nTheFee / COIN);
                    vector<CAliasFee> vAliasFees(lstAliasFees.begin(), lstAliasFees.end());
                    if (!paliasdb->WriteNameTxFees(vAliasFees))
                        return error( "CheckOfferInputs() : failed to write fees to alias DB");

                    // write alias to alias index
                    bool bFound = false;
                    BOOST_FOREACH(vector<unsigned char> &vch, vecNameIndex) {
                        if(vch == vvchArgs[0]) { bFound = true; break; }
                    }
                    if(!bFound) vecNameIndex.push_back(vvchArgs[0]);
                    if (!paliasdb->WriteNameIndex(vecNameIndex))
                        return error("CheckAliasInputs() : failed to write index to alias DB");

                    printf("CONNECTED ALIAS: name=%s  op=%s  hash=%s  height=%d\n",
                        stringFromVch(vvchArgs[0]).c_str(),
                        aliasFromOp(op).c_str(),
                        tx.GetHash().ToString().c_str(),
                        nHeight);
                }
            }

            if(pindexBlock->nHeight != pindexBest->nHeight) {

				if (op != OP_ALIAS_NEW) {
					LOCK(cs_main);
					std::map<std::vector<unsigned char>, std::set<uint256> >::iterator mi = mapAliasesPending.find(vvchArgs[0]);
					if (mi != mapAliasesPending.end())
						mi->second.erase(tx.GetHash());
				} else {
					InsertAliasFee(pindexBlock, tx.GetHash(), GetAliasNetworkFee(pindexBlock->nHeight));
				}
            }
        }
    }
    return true;
}

bool ExtractAliasAddress(const CScript& script, string& address) {
    if (script.size() == 1 && script[0] == OP_RETURN) {
        address = string("network fee");
        return true;
    }
    vector<vector<unsigned char> > vvch;
    int op;
    if (!DecodeAliasScript(script, op, vvch))
        return false;

    string strOp = aliasFromOp(op);
    string strName;
    if (op == OP_ALIAS_NEW) {
#ifdef GUI
        LOCK(cs_main);

        std::map<uint160, std::vector<unsigned char> >::const_iterator mi = mapMyNameHashes.find(uint160(vvch[0]));
        if (mi != mapMyNameHashes.end())
            strName = stringFromVch(mi->second);
        else
#endif
            strName = HexStr(vvch[0]);
    }
    else
        strName = stringFromVch(vvch[0]);

    address = strOp + ": " + strName;
    return true;
}


int64 getAmount(Value value) {
    ConvertTo<double>(value);
    double dAmount = value.get_real();
    int64 nAmount = roundint64(dAmount * COIN);
    if (!MoneyRange(nAmount))
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount");
    return nAmount;
}

vector<unsigned char> vchFromValue(const Value& value) {
    string strName = value.get_str();
    unsigned char *strbeg = (unsigned char*)strName.c_str();
    return vector<unsigned char>(strbeg, strbeg + strName.size());
}

std::vector<unsigned char> vchFromString(const std::string &str) {
    unsigned char *strbeg = (unsigned char*)str.c_str();
    return vector<unsigned char>(strbeg, strbeg + str.size());
}

string stringFromVch(const vector<unsigned char> &vch) {
    string res;
    vector<unsigned char>::const_iterator vi = vch.begin();
    while (vi != vch.end()) {
        res += (char)(*vi);
        vi++;
    }
    return res;
}

bool CNameDB::ScanNames(
        const std::vector<unsigned char>& vchName,
        int nMax,
        std::vector<std::pair<std::vector<unsigned char>, CAliasIndex> >& nameScan) {
    return true;
}

void rescanforaliases(CBlockIndex *pindexRescan) {
    printf("Scanning blockchain for names to create fast index...\n");
    paliasdb->ReconstructNameIndex(pindexRescan);
}

bool CNameDB::ReconstructNameIndex(CBlockIndex *pindexRescan) {
    CDiskTxPos txindex;
    CBlockIndex* pindex = pindexRescan;

    {
	LOCK(pwalletMain->cs_wallet);
    while (pindex) {
        CBlock block;
        block.ReadFromDisk(pindex);
        int nHeight = pindex->nHeight;
        uint256 txblkhash;

        BOOST_FOREACH(CTransaction& tx, block.vtx) {

            if (tx.nVersion != SYSCOIN_TX_VERSION)
                continue;

            vector<vector<unsigned char> > vvchArgs;
            int op, nOut;

            // decode the alias op
            bool o = DecodeAliasTx(tx, op, nOut, vvchArgs, nHeight);
            if (!o || !IsAliasOp(op)) continue;
            if (op == OP_ALIAS_NEW) continue;

            const vector<unsigned char> &vchName = vvchArgs[0];
            const vector<unsigned char> &vchValue = vvchArgs[op == OP_ALIAS_ACTIVATE ? 2 : 1];

            if(!GetTransaction(tx.GetHash(), tx, txblkhash, true))
                continue;

            // if name exists in DB, read it to verify
            vector<CAliasIndex> vtxPos;
            if (ExistsName(vchName)) {
                if (!ReadName(vchName, vtxPos))
                    return error("ReconstructNameIndex() : failed to read from alias DB");
            }

            // rebuild the alias object, store to DB
            CAliasIndex txName;
            txName.nHeight = nHeight;
            txName.vValue = vchValue;
            txName.txHash = tx.GetHash();
            vtxPos.push_back(txName);
            if (!WriteName(vchName, vtxPos))
                return error("ReconstructNameIndex() : failed to write to alias DB");

            // get fees for txn and add them to regenerate list
            int64 nTheFee = GetAliasNetFee(tx);
            InsertAliasFee(pindex, tx.GetHash(), nTheFee);
            if (!paliasdb->WriteNameIndex(vecNameIndex))
                return error("ReconstructNameIndex() : failed to write index to alias DB");

            printf( "RECONSTRUCT ALIAS: op=%s alias=%s value=%s hash=%s height=%d fees=%llu\n",
                    aliasFromOp(op).c_str(),
                    stringFromVch(vchName).c_str(),
                    stringFromVch(vchValue).c_str(),
                    tx.GetHash().ToString().c_str(), 
                    nHeight, nTheFee / COIN);

            // add alias to master alias index, add it to my names map
            // if it is owned by this client
            vecNameIndex.push_back(vvchArgs[0]);
            if(IsAliasMine(tx)) {
                mapMyAliases[vvchArgs[0]] = tx.GetHash();
            }
        }
        pindex = pindex->pnext;
        Flush();
    }
    }
    return true;
}

// 10080 blocks = 1 week
// alias expiration time is ~ 6 months or 26 weeks
// expiration blocks is 262080 (final)
// expiration starts at 87360, increases by 1 per block starting at
// block 174721 until block 349440

//
// Increase expiration to 262080 gradually starting at block 174720.
// Use for validation purposes and pass the chain height.
int GetAliasExpirationDepth(int nHeight) {
    if (nHeight < 174720) return 87360;
    if (nHeight < 349440) return nHeight - 87360;
    return 262080;
}

// For display purposes, pass the name height.
int GetNameDisplayExpirationDepth(int nHeight) {
    if (nHeight < 87360) return 87360;
    return 262080;
}

int64 GetAliasNetworkFee(int nHeight) {
    // Speed up network fee decrease 4x starting at 174720
    if (nHeight >= 174720) nHeight += (nHeight - 174720) * 3;
    if ((nHeight >> 13) >= 60) return 0;
    int64 nStart = 50 * COIN;
    if (fTestNet) nStart = 10 * CENT;
    int64 nRes = nStart >> (nHeight >> 13);
    nRes -= (nRes >> 14) * (nHeight % 8192);
    return nRes;
}

int GetNameTxPosHeight(const CDiskTxPos& txPos) {
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

int GetNameTxPosHeight2(const CDiskTxPos& txPos, int nHeight) {
    nHeight = GetNameTxPosHeight(txPos);
    return nHeight;
}

CScript RemoveAliasScriptPrefix(const CScript& scriptIn) {
    int op;
    vector<vector<unsigned char> > vvch;
    CScript::const_iterator pc = scriptIn.begin();

    if (!DecodeAliasScript(scriptIn, op, vvch,  pc))
        throw runtime_error("RemoveAliasScriptPrefix() : could not decode name script");
    return CScript(pc, scriptIn.end());
}

bool SignNameSignature(const CTransaction& txFrom, CTransaction& txTo, unsigned int nIn, int nHashType=SIGHASH_ALL, CScript scriptPrereq=CScript()) {
    assert(nIn < txTo.vin.size());
    CTxIn& txin = txTo.vin[nIn];
    assert(txin.prevout.n < txFrom.vout.size());
    const CTxOut& txout = txFrom.vout[txin.prevout.n];

    // Leave out the signature from the hash, since a signature can't sign itself.
    // The checksig op will also drop the signatures from its hash.
    const CScript& scriptPubKey = RemoveAliasScriptPrefix(txout.scriptPubKey);
    uint256 hash = SignatureHash(scriptPrereq + txout.scriptPubKey, txTo, nIn, nHashType);
    txnouttype whichTypeRet;

    if (!Solver(*pwalletMain, scriptPubKey, hash, nHashType, txin.scriptSig, whichTypeRet))
        return false;

    txin.scriptSig = scriptPrereq + txin.scriptSig;

    // Test the solution
    if (scriptPrereq.empty())
        if (!VerifyScript(txin.scriptSig, txout.scriptPubKey, txTo, nIn, 0, 0))
            return false;

    return true;
}

bool CreateTransactionWithInputTx(const vector<pair<CScript, int64> >& vecSend, CWalletTx& wtxIn, int nTxOut, CWalletTx& wtxNew, CReserveKey& reservekey, int64& nFeeRet, const string& txData) {
    int64 nValue = 0;
    BOOST_FOREACH(const PAIRTYPE(CScript, int64)& s, vecSend) {
        if (nValue < 0) return false;
        nValue += s.second;
    }
    if (vecSend.empty() || nValue < 0)
        return false;

    wtxNew.BindWallet(pwalletMain);

    {
    LOCK2(cs_main,pwalletMain->cs_wallet);

	nFeeRet = nTransactionFee;
	loop
	{
		wtxNew.vin.clear();
		wtxNew.vout.clear();
		wtxNew.fFromMe = true;
		wtxNew.data = vchFromString(txData);

		int64 nTotalValue = nValue + nFeeRet;
		printf("CreateTransactionWithInputTx: total value = %d\n", (int)nTotalValue);
		double dPriority = 0;

		// vouts to the payees
		BOOST_FOREACH(const PAIRTYPE(CScript, int64)& s, vecSend)
			wtxNew.vout.push_back(CTxOut(s.second, s.first));

		int64 nWtxinCredit = wtxIn.vout[nTxOut].nValue;

		// Choose coins to use
		set<pair<const CWalletTx*, unsigned int> > setCoins;
		int64 nValueIn = 0;
		printf("CreateTransactionWithInputTx: SelectCoins(%s), nTotalValue = %s, nWtxinCredit = %s\n", FormatMoney(nTotalValue - nWtxinCredit).c_str(), FormatMoney(nTotalValue).c_str(), FormatMoney(nWtxinCredit).c_str());
		if (nTotalValue - nWtxinCredit > 0) {
			if (!pwalletMain->SelectCoins(nTotalValue - nWtxinCredit, setCoins, nValueIn))
				return false;
		}

		printf("CreateTransactionWithInputTx: selected %d tx outs, nValueIn = %s\n", (int)setCoins.size(), FormatMoney(nValueIn).c_str() );

		vector<pair<const CWalletTx*, unsigned int> >
			vecCoins(setCoins.begin(), setCoins.end());

		BOOST_FOREACH(PAIRTYPE(const CWalletTx*, unsigned int)& coin, vecCoins) {
			int64 nCredit = coin.first->vout[coin.second].nValue;
			dPriority += (double)nCredit * coin.first->GetDepthInMainChain();
		}

		// Input tx always at first position
		vecCoins.insert(vecCoins.begin(), make_pair(&wtxIn, nTxOut));

		nValueIn += nWtxinCredit;
		dPriority += (double)nWtxinCredit * wtxIn.GetDepthInMainChain();

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
			vector<CTxOut>::iterator position = wtxNew.vout.begin()+GetRandInt(wtxNew.vout.size());
			wtxNew.vout.insert(position, CTxOut(nChange, scriptChange));
		}
		else
			reservekey.ReturnKey();

		// Fill vin
		BOOST_FOREACH(PAIRTYPE(const CWalletTx*, unsigned int)& coin, vecCoins)
			wtxNew.vin.push_back(CTxIn(coin.first->GetHash(), coin.second));

		// Sign
		int nIn = 0;
		BOOST_FOREACH(PAIRTYPE(const CWalletTx*, unsigned int)& coin, vecCoins) {
			if (coin.first == &wtxIn && coin.second == (unsigned int) nTxOut) {
				if (!SignNameSignature(*coin.first, wtxNew, nIn++))
					throw runtime_error("could not sign name coin output");
			}
			else {
				if (!SignSignature(*pwalletMain, *coin.first, wtxNew, nIn++))
					return false;
			}
		}

		// Limit size
		unsigned int nBytes = ::GetSerializeSize(*(CTransaction*)&wtxNew, SER_NETWORK, PROTOCOL_VERSION);
		if (nBytes >= MAX_BLOCK_SIZE_GEN/5) return false;
		dPriority /= nBytes;

		// Check that enough fee is included
		int64 nPayFee = nTransactionFee * (1 + (int64)nBytes / 1000);
		bool fAllowFree = CTransaction::AllowFree(dPriority);
		int64 nMinFee = wtxNew.GetMinFee(1, fAllowFree);
		if (nFeeRet < max(nPayFee, nMinFee)) {
			nFeeRet = max(nPayFee, nMinFee);
			printf("CreateTransactionWithInputTx: re-iterating (nFreeRet = %s)\n", FormatMoney(nFeeRet).c_str());
			continue;
		}

		// Fill vtxPrev by copying from previous transactions vtxPrev
		wtxNew.AddSupportingTransactions();
		wtxNew.fTimeReceivedIsTxTime = true;

		break;
	}

    }

    printf("CreateTransactionWithInputTx succeeded:\n%s", wtxNew.ToString().c_str());
    return true;
}

// nTxOut is the output from wtxIn that we should grab
string SendMoneyWithInputTx(CScript scriptPubKey, int64 nValue, int64 nNetFee, CWalletTx& wtxIn, CWalletTx& wtxNew, bool fAskFee, const string& txData)
{
    int nTxOut = IndexOfNameOutput(wtxIn);
    CReserveKey reservekey(pwalletMain);
    int64 nFeeRequired;
    vector< pair<CScript, int64> > vecSend;
    vecSend.push_back(make_pair(scriptPubKey, nValue));

    if (nNetFee) {
        CScript scriptFee;
        scriptFee << OP_RETURN;
        vecSend.push_back(make_pair(scriptFee, nNetFee));
    }

    if (!CreateTransactionWithInputTx(vecSend, wtxIn, nTxOut, wtxNew, reservekey, nFeeRequired, txData)) {
        string strError;
        if (nValue + nFeeRequired > pwalletMain->GetBalance())
            strError = strprintf(_("Error: This transaction requires a transaction fee of at least %s because of its amount, complexity, or use of recently received funds "), FormatMoney(nFeeRequired).c_str());
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
        return _("Error: The transaction was rejected.  This might happen if some of the coins in your wallet were already spent, such as if you used a copy of wallet.dat and coins were spent in the copy but not marked as spent here.");

    return "";
}

int GetNameTxHashHeight(const uint256 txHash) {
	CDiskTxPos postx;
	pblocktree->ReadTxIndex(txHash, postx);
    return postx.nPos;
}

bool GetValueOfNameTxHash(const uint256 &txHash, vector<unsigned char>& vchValue, uint256& hash, int& nHeight) {
    nHeight = GetNameTxHashHeight(txHash);
    CTransaction tx;
    uint256 blockHash;
    if (!GetTransaction(txHash,tx, blockHash, true))
        return error("GetValueOfNameTxHash() : could not read tx from disk");
    if (!GetValueOfNameTx(tx, vchValue))
        return error("GetValueOfNameTxHash() : could not decode value from tx");
    hash = tx.GetHash();
    return true;
}

bool GetValueOfName(CNameDB& dbName, const vector<unsigned char> &vchName, vector<unsigned char>& vchValue, int& nHeight) {
    vector<CAliasIndex> vtxPos;
    if (!paliasdb->ReadName(vchName, vtxPos) || vtxPos.empty())
        return false;

    CAliasIndex& txPos = vtxPos.back();
    nHeight = txPos.nHeight;
    vchValue = txPos.vValue;
    return true;
}

bool GetTxOfName(CNameDB& dbName, const vector<unsigned char> &vchName, CTransaction& tx) {
    vector<CAliasIndex> vtxPos;
    if (!paliasdb->ReadName(vchName, vtxPos) || vtxPos.empty())
        return false;
    CAliasIndex& txPos = vtxPos.back();
    int nHeight = txPos.nHeight;
    if (nHeight + GetAliasExpirationDepth(pindexBest->nHeight) < pindexBest->nHeight) {
        string name = stringFromVch(vchName);
        printf("GetTxOfName(%s) : expired", name.c_str());
        return false;
    }

    uint256 hashBlock;
    if (!GetTransaction(txPos.txHash, tx, hashBlock, true))
        return error("GetTxOfName() : could not read tx from disk");

    return true;
}

bool GetNameAddress(const CTransaction& tx, std::string& strAddress) {
    int op, nOut = 0;
    vector<vector<unsigned char> > vvch;

    if(!DecodeAliasTx(tx, op, nOut, vvch, -1))
    	return error("GetNameAddress() : could not decode name tx.");

    const CTxOut& txout = tx.vout[nOut];

    const CScript& scriptPubKey = RemoveAliasScriptPrefix(txout.scriptPubKey);
    strAddress = CBitcoinAddress(scriptPubKey.GetID()).ToString();
    return true;
}

bool GetNameAddress(const CDiskTxPos& txPos, std::string& strAddress) {
    CTransaction tx;
    if (!tx.ReadFromDisk(txPos))
        return error("GetNameAddress() : could not read tx from disk");
    return GetNameAddress(tx, strAddress);
}

/*
Value sendtoname(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 2 || params.size() > 4)
        throw runtime_error(
            "sendtoname <syscoinname> <amount> [comment] [comment-to]\n"
            "<amount> is a real and is rounded to the nearest 0.01"
            + HelpRequiringPassphrase());

    vector<unsigned char> vchName = vchFromValue(params[0]);
    //CNameDB dbName("r");
    if (!paliasdb->ExistsName(vchName))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Name not found");

    string strAddress;
    CTransaction tx;
    GetTxOfName(dbName, vchName, tx);
    GetNameAddress(tx, strAddress);

    uint160 hash160;
    if (!AddressToHash160(strAddress.c_str(), hash160))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No valid syscoin address");

    // Amount
    int64 nAmount = AmountFromValue(params[1]);

    // Wallet comments
    CWalletTx wtx;
    if (params.size() > 2 && params[2].type() != null_type && !params[2].get_str().empty())
        wtx.mapValue["comment"] = params[2].get_str();
    if (params.size() > 3 && params[3].type() != null_type && !params[3].get_str().empty())
        wtx.mapValue["to"]      = params[3] .get_str();

	{
    LOCK(cs_main);

        EnsureWalletIsUnlocked();

	string strError = pwalletMain->SendMoneyToDestination(strAddress, nAmount, wtx);
	if (strError != "")
		throw JSONRPCError(RPC_WALLET_ERROR, strError);

    }

    return wtx.GetHash().GetHex();
}
*/
int IndexOfNameOutput(const CTransaction& tx) {
    vector<vector<unsigned char> > vvch;

    int op;
    int nOut;
    bool good = DecodeAliasTx(tx, op, nOut, vvch, -1);

    if (!good)
        throw runtime_error("IndexOfNameOutput() : name output not found");
    return nOut;
}


bool GetNameOfTx(const CTransaction& tx, vector<unsigned char>& name) {
    if (tx.nVersion != SYSCOIN_TX_VERSION)
        return false;
    vector<vector<unsigned char> > vvchArgs;
    int op;
    int nOut;

    bool good = DecodeAliasTx(tx, op, nOut, vvchArgs, -1);
    if (!good)
        return error("GetNameOfTx() : could not decode a syscoin tx");

    switch (op) {
        case OP_ALIAS_ACTIVATE:
        case OP_ALIAS_UPDATE:
            name = vvchArgs[0];
            return true;
    }
    return false;
}

bool IsConflictedAliasTx(CBlockTreeDB& txdb, const CTransaction& tx, vector<unsigned char>& name) {
    if (tx.nVersion != SYSCOIN_TX_VERSION)
        return false;
    vector<vector<unsigned char> > vvchArgs;
    int op;
    int nOut;

    bool good = DecodeAliasTx(tx, op, nOut, vvchArgs, pindexBest->nHeight);
    if (!good)
        return error("IsConflictedAliasTx() : could not decode a syscoin tx");
    int nPrevHeight;

    switch (op) {
        case OP_ALIAS_ACTIVATE:
            nPrevHeight = GetAliasHeight(vvchArgs[0]);
            name = vvchArgs[0];
            if (nPrevHeight >= 0 && pindexBest->nHeight - nPrevHeight < GetAliasExpirationDepth(pindexBest->nHeight))
                return true;
    }
    return false;
}

bool GetValueOfNameTx(const CTransaction& tx, vector<unsigned char>& value)
{
    vector<vector<unsigned char> > vvch;

    int op;
    int nOut;

    if (!DecodeAliasTx(tx, op, nOut, vvch, -1))
        return false;

    switch (op) {
        case OP_ALIAS_NEW:
            return false;
        case OP_ALIAS_ACTIVATE:
            value = vvch[2];
            return true;
        case OP_ALIAS_UPDATE:
            value = vvch[1];
            return true;
        default:
            return false;
    }
}

bool DecodeAliasTx(const CTransaction& tx, int& op, int& nOut, vector<vector<unsigned char> >& vvch, int nHeight) {
    bool found = false;

    if (nHeight < 0)
        nHeight = pindexBest->nHeight;

	// Strict check - bug disallowed
	for (unsigned int i = 0; i < tx.vout.size(); i++) {
		const CTxOut& out = tx.vout[i];
		vector<vector<unsigned char> > vvchRead;
		if (DecodeAliasScript(out.scriptPubKey, op, vvchRead)) {
			nOut = i;
			found = true;
			vvch = vvchRead;
			break;
		}
	}
	if (!found) vvch.clear();

    return found;
}

bool GetValueOfNameTx(const CCoins& tx, vector<unsigned char>& value) {
    vector<vector<unsigned char> > vvch;

    int op;
    int nOut;

    if (!DecodeAliasTx(tx, op, nOut, vvch, -1))
        return false;

    switch (op) {
        case OP_ALIAS_NEW:
            return false;
        case OP_ALIAS_ACTIVATE:
            value = vvch[2];
            return true;
        case OP_ALIAS_UPDATE:
            value = vvch[1];
            return true;
        default:
            return false;
    }
}

bool DecodeAliasTx(const CCoins& tx, int& op, int& nOut, vector<vector<unsigned char> >& vvch, int nHeight) {
    bool found = false;

    if (nHeight < 0)
        nHeight = pindexBest->nHeight;

    // Strict check - bug disallowed
    for (unsigned int i = 0; i < tx.vout.size(); i++) {
        const CTxOut& out = tx.vout[i];
        vector<vector<unsigned char> > vvchRead;
        if (DecodeAliasScript(out.scriptPubKey, op, vvchRead)) {
            nOut = i;
            found = true;
            vvch = vvchRead;
            break;
        }
    }
    if (!found) vvch.clear();
    return found;
}


bool DecodeAliasScript(const CScript& script, int& op, vector<vector<unsigned char> > &vvch) {
    CScript::const_iterator pc = script.begin();
    return DecodeAliasScript(script, op, vvch, pc);
}

bool DecodeAliasScript(const CScript& script, int& op, vector<vector<unsigned char> > &vvch, CScript::const_iterator& pc) {
    opcodetype opcode;
    if (!script.GetOp(pc, opcode))
        return false;
    if (opcode < OP_1 || opcode > OP_16)
        return false;

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

    if ((op == OP_ALIAS_NEW && vvch.size() == 1) ||
            (op == OP_ALIAS_ACTIVATE && vvch.size() == 3) ||
            (op == OP_ALIAS_UPDATE && vvch.size() == 2))
        return true;
    return false;
}

