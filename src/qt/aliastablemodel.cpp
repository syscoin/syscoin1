#include "aliastablemodel.h"

#include "guiutil.h"
#include "walletmodel.h"

#include "wallet.h"
#include "base58.h"

#include <QFont>

using namespace std;
bool GetValueOfAliasTxHash(const uint256 &txHash, vector<unsigned char>& vchValue, uint256& hash, int& nHeight);

const QString AliasTableModel::Alias = "A";
const QString AliasTableModel::DataAlias = "D";

class CAliasDB;

extern CAliasDB *paliasdb;

int GetAliasExpirationDepth(int nHeight);
int64 GetAliasTxHashHeight(const uint256 txHash);
struct AliasTableEntry
{
    enum Type {
        Alias,
        DataAlias
    };

    Type type;
    QString value;
    QString alias;
    QString expirationdepth;

    AliasTableEntry() {}
    AliasTableEntry(Type type, const QString &value, const QString &alias, const QString &expirationdepth):
        type(type), value(value), alias(alias), expirationdepth(expirationdepth) {}
};

struct AliasTableEntryLessThan
{
    bool operator()(const AliasTableEntry &a, const AliasTableEntry &b) const
    {
        return a.alias < b.alias;
    }
    bool operator()(const AliasTableEntry &a, const QString &b) const
    {
        return a.alias < b;
    }
    bool operator()(const QString &a, const AliasTableEntry &b) const
    {
        return a < b.alias;
    }
};

#define NAMEMAPTYPE map<vector<unsigned char>, uint256>

// Private implementation
class AliasTablePriv
{
public:
    CWallet *wallet;
    QList<AliasTableEntry> cachedAliasTable;
    AliasTableModel *parent;

    AliasTablePriv(CWallet *wallet, AliasTableModel *parent):
        wallet(wallet), parent(parent) {}

    void refreshAliasTable(AliasModelType type)
    {

        cachedAliasTable.clear();
        {
            TRY_LOCK(wallet->cs_wallet, cs_trywallet);
			BOOST_FOREACH(PAIRTYPE(const uint256, CWalletTx)& item, wallet->mapWallet)
			{
					// get txn hash, read txn index
					CTransaction &tx = item.second;
					CTransaction lastTx;
					// skip non-syscoin txns
					if (tx.nVersion != SYSCOIN_TX_VERSION)
						continue;

                    int op, nOut, nHeight;
					
                    vector<vector<unsigned char> > vvchArgs;
					vector<unsigned char> vchValue;
                    bool o = DecodeAliasTx(tx, op, nOut, vvchArgs, -1);
					// prune out this tx if its not an alias that is yours
                    if (type != MyAlias || !o || !IsAliasOp(op) || !IsAliasMine(tx)) continue;
                    if (op == OP_ALIAS_NEW) continue;
					  
					// check for alias existence in DB (check that the alias wasn't transferred)
					vector<CAliasIndex> vtxPos;
					if (!paliasdb->ReadAlias(vvchArgs[0], vtxPos))
						continue;
					if (vtxPos.size() < 1)
						continue;
					uint256 hashBlock;
					uint256 txHash = vtxPos.back().txHash;
					if (!GetTransaction(txHash, lastTx, hashBlock, true))
						continue;
					// alias was transferred because it was mine before, but the last alias address isn't yours
					if (!IsAliasMine(lastTx)) continue; 
					// get alias data
					
					
					if (!GetValueOfAliasTxHash(txHash, vchValue, hashBlock, nHeight))
						continue;

					unsigned long nExpDepth = nHeight + GetAliasExpirationDepth(nHeight) - pindexBest->nHeight;
					std::string strExpDepth;
					if(nExpDepth <= 0) 
						strExpDepth = "Expired";
					else
						strExpDepth = strprintf("%lu", nExpDepth);
					updateEntry(QString::fromStdString(stringFromVch(vvchArgs[0])), QString::fromStdString(stringFromVch(vchValue)), QString::fromStdString(strExpDepth),type, CT_NEW); 
					
		

            }
         }
        // qLowerBound() and qUpperBound() require our cachedAliasTable list to be sorted in asc order
        qSort(cachedAliasTable.begin(), cachedAliasTable.end(), AliasTableEntryLessThan());
    }

    void updateEntry(const QString &alias, const QString &value, const QString &exp, AliasModelType type, int status)
    {
		if(!parent || parent->modelType != type)
		{
			return;
		}
        // Find alias / value in model
        QList<AliasTableEntry>::iterator lower = qLowerBound(
            cachedAliasTable.begin(), cachedAliasTable.end(), alias, AliasTableEntryLessThan());
        QList<AliasTableEntry>::iterator upper = qUpperBound(
            cachedAliasTable.begin(), cachedAliasTable.end(), alias, AliasTableEntryLessThan());
        int lowerIndex = (lower - cachedAliasTable.begin());
        int upperIndex = (upper - cachedAliasTable.begin());
        bool inModel = (lower != upper);
		int index;
        AliasTableEntry::Type newEntryType = /*isData ? AliasTableEntry::DataAlias :*/ AliasTableEntry::Alias;

        switch(status)
        {
        case CT_NEW:
			index = parent->lookupAlias(alias);
            if(inModel || index != -1)
            {
                OutputDebugStringF("Warning: AliasTablePriv::updateEntry: Got CT_NOW, but entry is already in model\n");
                break;
            
            }
            parent->beginInsertRows(QModelIndex(), lowerIndex, lowerIndex);
            cachedAliasTable.insert(lowerIndex, AliasTableEntry(newEntryType, value, alias, exp));
            parent->endInsertRows();
            break;
        case CT_UPDATED:
            if(!inModel)
            {
                OutputDebugStringF("Warning: AliasTablePriv::updateEntry: Got CT_UPDATED, but entry is not in model\n");
                break;
            }
            lower->type = newEntryType;
            lower->value = value;
            lower->expirationdepth = exp;
            parent->emitDataChanged(lowerIndex);
            break;
        case CT_DELETED:
            if(!inModel)
            {
                OutputDebugStringF("Warning: AliasTablePriv::updateEntry: Got CT_DELETED, but entry is not in model\n");
                break;
            }
            parent->beginRemoveRows(QModelIndex(), lowerIndex, upperIndex-1);
            cachedAliasTable.erase(lower, upper);
            parent->endRemoveRows();
            break;
        }
    }

    int size()
    {
        return cachedAliasTable.size();
    }

    AliasTableEntry *index(int idx)
    {
        if(idx >= 0 && idx < cachedAliasTable.size())
        {
            return &cachedAliasTable[idx];
        }
        else
        {
            return 0;
        }
    }
};

AliasTableModel::AliasTableModel(CWallet *wallet, WalletModel *parent,  AliasModelType type) :
    QAbstractTableModel(parent),walletModel(parent),wallet(wallet),priv(0), modelType(type)
{

	columns << tr("Alias") << tr("Value") << tr("Expires In");		 
    priv = new AliasTablePriv(wallet, this);
	refreshAliasTable();
}

AliasTableModel::~AliasTableModel()
{
    delete priv;
}
void AliasTableModel::refreshAliasTable() 
{
	clear();
	priv->refreshAliasTable(modelType);
}
int AliasTableModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return priv->size();
}

int AliasTableModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return columns.length();
}

QVariant AliasTableModel::data(const QModelIndex &index, int role) const
{
    if(!index.isValid())
        return QVariant();

    AliasTableEntry *rec = static_cast<AliasTableEntry*>(index.internalPointer());

    if(role == Qt::DisplayRole || role == Qt::EditRole)
    {
        switch(index.column())
        {
        case Value:
            return rec->value;
        case Name:
            return rec->alias;
        case ExpirationDepth:
            return rec->expirationdepth;
        }
    }
    else if (role == Qt::FontRole)
    {
        QFont font;
        if(index.column() == Value)
        {
            font = GUIUtil::bitcoinAddressFont();
        }
        return font;
    }
    else if (role == TypeRole)
    {
        switch(rec->type)
        {
        case AliasTableEntry::Alias:
            return Alias;
        case AliasTableEntry::DataAlias:
            return DataAlias;
        default: break;
        }
    }
    return QVariant();
}

bool AliasTableModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if(!index.isValid())
        return false;
    AliasTableEntry *rec = static_cast<AliasTableEntry*>(index.internalPointer());

    editStatus = OK;

    if(role == Qt::EditRole)
    {
        switch(index.column())
        {
        case ExpirationDepth:
            // Do nothing, if old value == new value
            if(rec->expirationdepth == value.toString())
            {
                editStatus = NO_CHANGES;
                return false;
            }
            //wallet->SetAddressBookName(CBitcoinAlias(rec->alias.toStdString()).Get(), value.toString().toStdString());
            break;
        case Value:
            // Do nothing, if old value == new value
            if(rec->value == value.toString())
            {
                editStatus = NO_CHANGES;
                return false;
            }
            //wallet->SetAddressBookName(CBitcoinAlias(rec->alias.toStdString()).Get(), value.toString().toStdString());
            break;
        case Name:
            // Do nothing, if old alias == new alias
            if(rec->alias == value.toString())
            {
                editStatus = NO_CHANGES;
                return false;
            }
            // Check for duplicate aliases to prevent accidental deletion of aliases, if you try
            // to paste an existing alias over another alias (with a different label)
            else if(lookupAlias(rec->alias) != -1)
            {
                editStatus = DUPLICATE_ALIAS;
                return false;
            }
            // Double-check that we're not overwriting a receiving alias
            else if(rec->type == AliasTableEntry::Alias)
            {
                {
                    // update alias
                }
            }
            break;
        }
        return true;
    }
    return false;
}

QVariant AliasTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if(orientation == Qt::Horizontal)
    {
        if(role == Qt::DisplayRole)
        {
            return columns[section];
        }
    }
    return QVariant();
}

Qt::ItemFlags AliasTableModel::flags(const QModelIndex &index) const
{
    if(!index.isValid())
        return 0;
    Qt::ItemFlags retval = Qt::ItemIsSelectable | Qt::ItemIsEnabled;
    return retval;
}

QModelIndex AliasTableModel::index(int row, int column, const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    AliasTableEntry *data = priv->index(row);
    if(data)
    {
        return createIndex(row, column, priv->index(row));
    }
    else
    {
        return QModelIndex();
    }
}

void AliasTableModel::updateEntry(const QString &alias, const QString &value, const QString &exp, AliasModelType type, int status)
{
    // Update alias book model from Bitcoin core
    priv->updateEntry(alias, value, exp, type, status);
}

QString AliasTableModel::addRow(const QString &type, const QString &value, const QString &alias, const QString &exp)
{
    std::string strValue = value.toStdString();
    std::string strAlias = alias.toStdString();
    std::string strExp = exp.toStdString();

    editStatus = OK;
    // Check for duplicate aliases
    {
        LOCK(wallet->cs_wallet);
        if(lookupAlias(alias) != -1)
        {
            editStatus = DUPLICATE_ALIAS;
            return QString();
        }
    }

    // Add entry

    return QString::fromStdString(strAlias);
}
void AliasTableModel::clear()
{
	beginResetModel();
    priv->cachedAliasTable.clear();
	endResetModel();
}

/* Look up value for alias, if not found return empty string.
 */
QString AliasTableModel::valueForAlias(const QString &alias) const
{
	CBitcoinAddress address_parsed(alias.toStdString());
	if(address_parsed.IsValid() && address_parsed.isAlias)
		return QString::fromStdString(address_parsed.ToString());
    return QString::fromStdString("{}");
}

int AliasTableModel::lookupAlias(const QString &alias) const
{
    QModelIndexList lst = match(index(0, Name, QModelIndex()),
                                Qt::EditRole, alias, 1, Qt::MatchExactly);
    if(lst.isEmpty())
    {
        return -1;
    }
    else
    {
        return lst.at(0).row();
    }
}

void AliasTableModel::emitDataChanged(int idx)
{
    emit dataChanged(index(idx, 0, QModelIndex()), index(idx, columns.length()-1, QModelIndex()));
}
