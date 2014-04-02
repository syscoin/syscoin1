#include "certtablemodel.h"

#include "guiutil.h"
#include "walletmodel.h"

#include "wallet.h"
#include "base58.h"

#include <QFont>

using namespace std;

const QString CertIssuerTableModel::CertIssuer = "O";
const QString CertIssuerTableModel::CertItem = "A";

class CCertDB;

extern CCertDB *pcertdb;

int GetCertIssuerExpirationDepth(int nHeight);

struct CertIssuerTableEntry
{
    enum Type {
        CertIssuer,
        CertItem
    };

    Type type;
    QString title;
    QString cert;
    QString category;
    QString price;
    QString quantity;
    QString expirationdepth;

    CertIssuerTableEntry() {}
    CertIssuerTableEntry(Type type, const QString &title, const QString &cert, const QString &category, const QString &price, const QString &quantity, const QString &expirationdepth):
        type(type), title(title), cert(cert), category(category), price(price), quantity(quantity), expirationdepth(expirationdepth) {}
};

struct CertIssuerTableEntryLessThan
{
    bool operator()(const CertIssuerTableEntry &a, const CertIssuerTableEntry &b) const
    {
        return a.cert < b.cert;
    }
    bool operator()(const CertIssuerTableEntry &a, const QString &b) const
    {
        return a.cert < b;
    }
    bool operator()(const QString &a, const CertIssuerTableEntry &b) const
    {
        return a < b.cert;
    }
};

#define NAMEMAPTYPE map<vector<unsigned char>, uint256>

// Private implementation
class CertIssuerTablePriv
{
public:
    CWallet *wallet;
    QList<CertIssuerTableEntry> cachedCertIssuerTable;
    CertIssuerTableModel *parent;

    CertIssuerTablePriv(CWallet *wallet, CertIssuerTableModel *parent):
        wallet(wallet), parent(parent) {}

    void refreshCertIssuerTable()
    {
        cachedCertIssuerTable.clear();
        {
            LOCK(wallet->cs_wallet);
            for(unsigned int i=0; i< vecCertIssuerIndex.size(); i++) {
                vector<unsigned char> cert = vecCertIssuerIndex[i];

                vector<CCertIssuer> vtxPos;
                if (pcertdb->ExistsCertIssuer(cert)) {
                    if (!pcertdb->ReadCertIssuer(cert, vtxPos))
                        continue;
                } else continue;
                uint256 txblkhash, txHash = vtxPos.back().txHash;
                CTransaction tx;

                // get the transaction
                if(!GetTransaction(txHash, tx, txblkhash, true))
                    continue;

                vector<vector<unsigned char> > vvchArgs;
                int op, nOut;
                if (!DecodeCertIssuerTx(tx, op, nOut, vvchArgs, -1)) {
                    OutputDebugStringF("refreshCertIssuerTable() : could not decode a syscoin tx");
                    continue;
                }

                if(!IsCertIssuerOp(op))
                    continue;

                // unserialize cert object from txn, check for valid
                CCertIssuer theCertIssuer;
                theCertIssuer.UnserializeFromTx(tx);
                if (theCertIssuer.IsNull()) {
                    OutputDebugStringF("refreshCertIssuerTable() : null cert object");
                    continue;
                }

                CCertItem theCertItem;
                double nPrice = theCertIssuer.nPrice / COIN;
                int nQty = theCertIssuer.nQty;
                unsigned long nExpDepth = GetCertIssuerExpirationDepth(theCertIssuer.nHeight);

                if(op == OP_CERT_NEW || op == OP_CERT_TRANSFER) {
                    if(!theCertIssuer.GetCertItemByHash(vvchArgs[1], theCertItem)) {
                        OutputDebugStringF("refreshCertIssuerTable() : failed to read accept from cert\n");
                        continue;
                    }
                    nPrice = theCertItem.nPrice / COIN;
                    nQty = theCertItem.nQty;
                }

                bool fIsCertItem = (op == OP_CERT_NEW || op == OP_CERT_TRANSFER);
                cachedCertIssuerTable.append(CertIssuerTableEntry(fIsCertItem ? CertIssuerTableEntry::CertItem : CertIssuerTableEntry::CertIssuer,
                                  QString::fromStdString(stringFromVch(theCertIssuer.sTitle)),
                                  QString::fromStdString(stringFromVch(cert)),
                                  QString::fromStdString(stringFromVch(theCertIssuer.sCategory)),
                                  QString::fromStdString(strprintf("%lf", nPrice)),
                                  QString::fromStdString(strprintf("%d", nQty)),
                                  QString::fromStdString(strprintf("%lu", nExpDepth))));
            }
        }
        // qLowerBound() and qUpperBound() require our cachedCertIssuerTable list to be sorted in asc order
        qSort(cachedCertIssuerTable.begin(), cachedCertIssuerTable.end(), CertIssuerTableEntryLessThan());
    }

    void updateEntry(const QString &cert, const QString &title, const QString &category,
                     const QString &price, const QString &quantity, const QString &expdepth, bool isCertItem, int status)
    {
        // Find cert / value in model
        QList<CertIssuerTableEntry>::iterator lower = qLowerBound(
            cachedCertIssuerTable.begin(), cachedCertIssuerTable.end(), cert, CertIssuerTableEntryLessThan());
        QList<CertIssuerTableEntry>::iterator upper = qUpperBound(
            cachedCertIssuerTable.begin(), cachedCertIssuerTable.end(), cert, CertIssuerTableEntryLessThan());
        int lowerIndex = (lower - cachedCertIssuerTable.begin());
        int upperIndex = (upper - cachedCertIssuerTable.begin());
        bool inModel = (lower != upper);
        CertIssuerTableEntry::Type newEntryType = isCertItem ? CertIssuerTableEntry::CertItem : CertIssuerTableEntry::CertIssuer;

        switch(status)
        {
        case CT_NEW:
            if(inModel)
            {
                OutputDebugStringF("Warning: CertIssuerTablePriv::updateEntry: Got CT_NOW, but entry is already in model\n");
                break;
            }
            parent->beginInsertRows(QModelIndex(), lowerIndex, lowerIndex);
            cachedCertIssuerTable.insert(lowerIndex, CertIssuerTableEntry(newEntryType, title, cert, category, price, quantity, expdepth));
            parent->endInsertRows();
            break;
        case CT_UPDATED:
            if(!inModel)
            {
                OutputDebugStringF("Warning: CertIssuerTablePriv::updateEntry: Got CT_UPDATED, but entry is not in model\n");
                break;
            }
            lower->type = newEntryType;
            lower->title = title;
            lower->category = category;
            lower->price = price;
            lower->quantity = quantity;
            lower->expirationdepth = expdepth;
            parent->emitDataChanged(lowerIndex);
            break;
        case CT_DELETED:
            if(!inModel)
            {
                OutputDebugStringF("Warning: CertIssuerTablePriv::updateEntry: Got CT_DELETED, but entry is not in model\n");
                break;
            }
            parent->beginRemoveRows(QModelIndex(), lowerIndex, upperIndex-1);
            cachedCertIssuerTable.erase(lower, upper);
            parent->endRemoveRows();
            break;
        }
    }

    int size()
    {
        return cachedCertIssuerTable.size();
    }

    CertIssuerTableEntry *index(int idx)
    {
        if(idx >= 0 && idx < cachedCertIssuerTable.size())
        {
            return &cachedCertIssuerTable[idx];
        }
        else
        {
            return 0;
        }
    }
};

CertIssuerTableModel::CertIssuerTableModel(CWallet *wallet, WalletModel *parent) :
    QAbstractTableModel(parent),walletModel(parent),wallet(wallet),priv(0)
{
    columns << tr("CertIssuer") << tr("Category") << tr("Title") << tr("Price") << tr("Quantity") << tr("Expiration Height");
    priv = new CertIssuerTablePriv(wallet, this);
    priv->refreshCertIssuerTable();
}

CertIssuerTableModel::~CertIssuerTableModel()
{
    delete priv;
}

int CertIssuerTableModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return priv->size();
}

int CertIssuerTableModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return columns.length();
}

QVariant CertIssuerTableModel::data(const QModelIndex &index, int role) const
{
    if(!index.isValid())
        return QVariant();

    CertIssuerTableEntry *rec = static_cast<CertIssuerTableEntry*>(index.internalPointer());

    if(role == Qt::DisplayRole || role == Qt::EditRole)
    {
        switch(index.column())
        {
        case Title:
            return rec->title;
        case Name:
            return rec->cert;
        case Category:
            return rec->category;
        case Price:
            return rec->price;
        case Quantity:
            return rec->quantity;
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
        case CertIssuerTableEntry::CertIssuer:
            return CertIssuer;
        case CertIssuerTableEntry::CertItem:
            return CertItem;
        default: break;
        }
    }
    return QVariant();
}

bool CertIssuerTableModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if(!index.isValid())
        return false;
    CertIssuerTableEntry *rec = static_cast<CertIssuerTableEntry*>(index.internalPointer());

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
        case Quantity:
            // Do nothing, if old value == new value
            if(rec->quantity == value.toString())
            {
                editStatus = NO_CHANGES;
                return false;
            }
            break;
        case Price:
            // Do nothing, if old value == new value
            if(rec->price == value.toString())
            {
                editStatus = NO_CHANGES;
                return false;
            }
            break;
        case Category:
            // Do nothing, if old value == new value
            if(rec->category == value.toString())
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
            else if(rec->type == CertIssuerTableEntry::CertIssuer)
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

QVariant CertIssuerTableModel::headerData(int section, Qt::Orientation orientation, int role) const
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

Qt::ItemFlags CertIssuerTableModel::flags(const QModelIndex &index) const
{
    if(!index.isValid())
        return 0;
    //CertIssuerTableEntry *rec = static_cast<CertIssuerTableEntry*>(index.internalPointer());

    Qt::ItemFlags retval = Qt::ItemIsSelectable | Qt::ItemIsEnabled;
    // only value is editable.
    if(index.column()==Title)
    {
        retval |= Qt::ItemIsEditable;
    }
    // return retval;
    return 0;
}

QModelIndex CertIssuerTableModel::index(int row, int column, const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    CertIssuerTableEntry *data = priv->index(row);
    if(data)
    {
        return createIndex(row, column, priv->index(row));
    }
    else
    {
        return QModelIndex();
    }
}

void CertIssuerTableModel::updateEntry(const QString &cert, const QString &title, const QString &category, const QString &price,
 const QString &quantity, const QString &expdepth, bool isMine, int status)
{
    // Update cert book model from Bitcoin core
    priv->updateEntry(cert, title, category, price, quantity, expdepth, isMine, status);
}

QString CertIssuerTableModel::addRow(const QString &type, const QString &title, const QString &cert, const QString &category, 
	const QString &price, const QString &quantity, const QString &expdepth)
{
    std::string strTitle = title.toStdString();
    std::string strCertIssuer = cert.toStdString();
    std::string strCategory = category.toStdString();
    std::string strPrice = price.toStdString();
    std::string strQuantity = quantity.toStdString();
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

    return QString::fromStdString(strCertIssuer);
}

bool CertIssuerTableModel::removeRows(int row, int count, const QModelIndex &parent)
{
    // refuse to remove certes.
    return false;
}

/* Look up value for cert, if not found return empty string.
 */
QString CertIssuerTableModel::valueForCertIssuer(const QString &cert) const
{
    return QString::fromStdString("{}");
}

int CertIssuerTableModel::lookupCertIssuer(const QString &cert) const
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

void CertIssuerTableModel::emitDataChanged(int idx)
{
    emit dataChanged(index(idx, 0, QModelIndex()), index(idx, columns.length()-1, QModelIndex()));
}
