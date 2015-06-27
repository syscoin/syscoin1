#include "certtablemodel.h"

#include "guiutil.h"
#include "walletmodel.h"

#include "wallet.h"
#include "base58.h"

#include <QFont>

using namespace std;

const QString CertTableModel::Cert = "O";


class CCertDB;

extern CCertDB *pcertdb;

int GetCertExpirationDepth(int nHeight);

struct CertTableEntry
{
    enum Type {
        Cert
    };

    Type type;
    QString title;
    QString cert;
    QString expirationdepth;

    CertTableEntry() {}
    CertTableEntry(Type type, const QString &title, const QString &cert, const QString &expirationdepth):
        type(type), title(title), cert(cert), expirationdepth(expirationdepth) {}
};

struct CertTableEntryLessThan
{
    bool operator()(const CertTableEntry &a, const CertTableEntry &b) const
    {
        return a.cert < b.cert;
    }
    bool operator()(const CertTableEntry &a, const QString &b) const
    {
        return a.cert < b;
    }
    bool operator()(const QString &a, const CertTableEntry &b) const
    {
        return a < b.cert;
    }
};

#define NAMEMAPTYPE map<vector<unsigned char>, uint256>

// Private implementation
class CertTablePriv
{
public:
    CWallet *wallet;
    QList<CertTableEntry> cachedCertTable;
    CertTableModel *parent;

    CertTablePriv(CWallet *wallet, CertTableModel *parent):
        wallet(wallet), parent(parent) {}

    void refreshCertTable()
    {
        cachedCertTable.clear();
        {
            CBlockIndex* pindex = pindexGenesisBlock;
            LOCK(wallet->cs_wallet);
            while (pindex) {
                int nHeight = pindex->nHeight;
                CBlock block;
                block.ReadFromDisk(pindex);
                uint256 txblkhash;

                BOOST_FOREACH(CTransaction& tx, block.vtx) {

                    if (tx.nVersion != SYSCOIN_TX_VERSION)
                        continue;

                    int op, nOut;
                    vector<vector<unsigned char> > vvchArgs;
                    bool o = DecodeCertTx(tx, op, nOut, vvchArgs, -1);
                    if (!o || !IsCertOp(op) || !IsCertMine(tx)) continue;

                    // get the transaction
                    if(!GetTransaction(tx.GetHash(), tx, txblkhash, true))
                        continue;

                    // attempt to read cert from txn
                    CCert theCert;
                    if(!theCert.UnserializeFromTx(tx))
                        continue;

                    vector<CCert> vtxPos;
                    if(!pcertdb->ReadCert(theCert.vchRand, vtxPos))
                        continue;

                    int nExpHeight = vtxPos.back().nHeight + GetCertExpirationDepth(vtxPos.back().nHeight);

               
                    if(op == OP_CERT_ACTIVATE)
                        cachedCertTable.append(CertTableEntry(CertTableEntry::Cert,
                                          QString::fromStdString(stringFromVch(theCert.vchTitle)),
                                          QString::fromStdString(stringFromVch(theCert.vchRand)),
                                          QString::fromStdString(strprintf("%d", nExpHeight ))));
                    
                }
                pindex = pindex->pnext;
            }
        }
        
        // qLowerBound() and qUpperBound() require our cachedCertTable list to be sorted in asc order
        qSort(cachedCertTable.begin(), cachedCertTable.end(), CertTableEntryLessThan());
    }

    void updateEntry(const QString &cert, const QString &title, const QString &expdepth, bool isCert, int status)
    {
        // Find cert / value in model
        QList<CertTableEntry>::iterator lower = qLowerBound(
            cachedCertTable.begin(), cachedCertTable.end(), cert, CertTableEntryLessThan());
        QList<CertTableEntry>::iterator upper = qUpperBound(
            cachedCertTable.begin(), cachedCertTable.end(), cert, CertTableEntryLessThan());
        int lowerIndex = (lower - cachedCertTable.begin());
        int upperIndex = (upper - cachedCertTable.begin());
        bool inModel = (lower != upper);
        CertTableEntry::Type newEntryType = isCert ? CertTableEntry::Cert : CertTableEntry::Cert;

        switch(status)
        {
        case CT_NEW:
            if(inModel)
            {
                OutputDebugStringF("Warning: CertTablePriv::updateEntry: Got CT_NOW, but entry is already in model\n");
                break;
            }
            parent->beginInsertRows(QModelIndex(), lowerIndex, lowerIndex);
            cachedCertTable.insert(lowerIndex, CertTableEntry(newEntryType, title, cert, expdepth));
            parent->endInsertRows();
            break;
        case CT_UPDATED:
            if(!inModel)
            {
                OutputDebugStringF("Warning: CertTablePriv::updateEntry: Got CT_UPDATED, but entry is not in model\n");
                break;
            }
            lower->type = newEntryType;
            lower->title = title;
            lower->expirationdepth = expdepth;
            parent->emitDataChanged(lowerIndex);
            break;
        case CT_DELETED:
            if(!inModel)
            {
                OutputDebugStringF("Warning: CertTablePriv::updateEntry: Got CT_DELETED, but entry is not in model\n");
                break;
            }
            parent->beginRemoveRows(QModelIndex(), lowerIndex, upperIndex-1);
            cachedCertTable.erase(lower, upper);
            parent->endRemoveRows();
            break;
        }
    }

    int size()
    {
        return cachedCertTable.size();
    }

    CertTableEntry *index(int idx)
    {
        if(idx >= 0 && idx < cachedCertTable.size())
        {
            return &cachedCertTable[idx];
        }
        else
        {
            return 0;
        }
    }
};

CertTableModel::CertTableModel(CWallet *wallet, WalletModel *parent) :
    QAbstractTableModel(parent),walletModel(parent),wallet(wallet),priv(0)
{
    columns << tr("Cert") << tr("Title") << tr("Expiration Height");
    priv = new CertTablePriv(wallet, this);
    priv->refreshCertTable();
}

CertTableModel::~CertTableModel()
{
    delete priv;
}

int CertTableModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return priv->size();
}

int CertTableModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return columns.length();
}

QVariant CertTableModel::data(const QModelIndex &index, int role) const
{
    if(!index.isValid())
        return QVariant();

    CertTableEntry *rec = static_cast<CertTableEntry*>(index.internalPointer());

    if(role == Qt::DisplayRole || role == Qt::EditRole)
    {
        switch(index.column())
        {
        case Title:
            return rec->title;
        case Name:
            return rec->cert;
        case ExpirationDepth:
            return rec->expirationdepth;
        }
    }
    else if (role == Qt::FontRole)
    {
        QFont font;
//        if(index.column() == Name)
//        {
//            font = GUIUtil::bitcoinAddressFont();
//        }
        return font;
    }
    else if (role == TypeRole)
    {
        switch(rec->type)
        {
        case CertTableEntry::Cert:
            return Cert;
        default: break;
        }
    }
    return QVariant();
}

bool CertTableModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if(!index.isValid())
        return false;
    CertTableEntry *rec = static_cast<CertTableEntry*>(index.internalPointer());

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
            break;
        case Title:
            // Do nothing, if old value == new value
            if(rec->title == value.toString())
            {
                editStatus = NO_CHANGES;
                return false;
            }
            break;
        case Name:
            // Do nothing, if old cert == new cert
            if(rec->cert == value.toString())
            {
                editStatus = NO_CHANGES;
                return false;
            }
            // Refuse to set invalid cert, set error status and return false
            else if(false /* validate cert */)
            {
                editStatus = INVALID_CERT;
                return false;
            }
            // Check for duplicate certes to prevent accidental deletion of certes, if you try
            // to paste an existing cert over another cert (with a different label)
            else if(false /* check duplicates */)
            {
                editStatus = DUPLICATE_CERT;
                return false;
            }
            // Double-check that we're not overwriting a receiving cert
            else if(rec->type == CertTableEntry::Cert)
            {
                {
                    // update cert
                }
            }
            break;
        }
        return true;
    }
    return false;
}

QVariant CertTableModel::headerData(int section, Qt::Orientation orientation, int role) const
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

Qt::ItemFlags CertTableModel::flags(const QModelIndex &index) const
{
    if(!index.isValid())
        return 0;
    //CertTableEntry *rec = static_cast<CertTableEntry*>(index.internalPointer());

    Qt::ItemFlags retval = Qt::ItemIsSelectable | Qt::ItemIsEnabled;
    // only value is editable.
    if(index.column()==Title)
    {
        retval |= Qt::ItemIsEditable;
    }
     return retval;
}

QModelIndex CertTableModel::index(int row, int column, const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    CertTableEntry *data = priv->index(row);
    if(data)
    {
        return createIndex(row, column, priv->index(row));
    }
    else
    {
        return QModelIndex();
    }
}

void CertTableModel::updateEntry(const QString &cert, const QString &title, const QString &expdepth, bool isMine, int status)
{
    // Update cert book model from Bitcoin core
    priv->updateEntry(cert, title, expdepth, isMine, status);
}

QString CertTableModel::addRow(const QString &type, const QString &title, const QString &cert, const QString &expdepth)
{
    std::string strTitle = title.toStdString();
    std::string strCert = cert.toStdString();
    std::string strExpdepth = expdepth.toStdString();

    editStatus = OK;

    if(false /*validate cert*/)
    {
        editStatus = INVALID_CERT;
        return QString();
    }
    // Check for duplicate certes
    {
        LOCK(wallet->cs_wallet);
        if(false/* check duplicate certes */)
        {
            editStatus = DUPLICATE_CERT;
            return QString();
        }
    }

    // Add entry

    return QString::fromStdString(strCert);
}

bool CertTableModel::removeRows(int row, int count, const QModelIndex &parent)
{
    // refuse to remove certes.
    return false;
}

/* Look up value for cert, if not found return empty string.
 */
QString CertTableModel::valueForCert(const QString &cert) const
{
    return QString::fromStdString("{}");
}

int CertTableModel::lookupCert(const QString &cert) const
{
    QModelIndexList lst = match(index(0, Name, QModelIndex()),
                                Qt::EditRole, cert, 1, Qt::MatchExactly);
    if(lst.isEmpty())
    {
        return -1;
    }
    else
    {
        return lst.at(0).row();
    }
}

void CertTableModel::emitDataChanged(int idx)
{
    emit dataChanged(index(idx, 0, QModelIndex()), index(idx, columns.length()-1, QModelIndex()));
}
