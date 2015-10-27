#include "certtablemodel.h"

#include "guiutil.h"
#include "walletmodel.h"

#include "wallet.h"
#include "base58.h"

#include <QFont>
using namespace std;
using namespace json_spirit;

const QString CertTableModel::Cert = "C";


extern const CRPCTable tableRPC;
struct CertTableEntry
{
    enum Type {
        Cert
    };

    Type type;
    QString title;
    QString cert;
	QString data;
	QString expires_on;
	QString expires_in;
	QString expired;

    CertTableEntry() {}
    CertTableEntry(Type type, const QString &title, const QString &cert, const QString &data, const QString &expires_on,const QString &expires_in, const QString &expired):
        type(type), title(title), cert(cert), data(data), expires_on(expires_on), expires_in(expires_in), expired(expired) {}
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

    void refreshCertTable(CertModelType type)
    {
        cachedCertTable.clear();
        {
			string strMethod = string("certlist");
	        Array params; 
			Value result ;
			string name_str;
			string data_str;
			string value_str;
			string expires_in_str;
			string expires_on_str;
			string expired_str;
			int expired = 0;
			int expires_in = 0;
			int expires_on = 0;
			

			try {
				result = tableRPC.execute(strMethod, params);
				if (result.type() == array_type)
				{
					name_str = "";
					value_str = "";
					expires_in_str = "";

					expires_on_str = "";

					expired = 0;
					expires_in = 0;
					expires_on = 0;

			
					Array arr = result.get_array();
					BOOST_FOREACH(Value& input, arr)
					{
						if (input.type() != obj_type)
							continue;
						Object& o = input.get_obj();
						name_str = "";
						value_str = "";
						data_str = "";
						expires_in_str = "";
		
						expires_on_str = "";
						expired = 0;
						expires_in = 0;
						expires_on = 0;

				
						const Value& name_value = find_value(o, "cert");
						if (name_value.type() == str_type)
							name_str = name_value.get_str();
						const Value& value_value = find_value(o, "title");
						if (value_value.type() == str_type)
							value_str = value_value.get_str();
						const Value& data_value = find_value(o, "data");
						if (data_value.type() == str_type)
							data_str = data_value.get_str();
						const Value& expires_on_value = find_value(o, "expires_on");
						if (expires_on_value.type() == int_type)
							expires_on = expires_on_value.get_int();
						const Value& expires_in_value = find_value(o, "expires_in");
						if (expires_in_value.type() == int_type)
							expires_in = expires_in_value.get_int();
						const Value& expired_value = find_value(o, "expired");
						if (expired_value.type() == int_type)
							expired = expired_value.get_int();
						if(expired == 1)
						{
							expired_str = "Expired";
						}
						else
						{
							expired_str = "Valid";
						}
						expires_in_str = strprintf("%d Blocks", expires_in);
						expires_on_str = strprintf("Block %d", expires_on);

						updateEntry(QString::fromStdString(name_str), QString::fromStdString(value_str), QString::fromStdString(data_str), QString::fromStdString(expires_on_str), QString::fromStdString(expires_in_str), QString::fromStdString(expired_str),type, CT_NEW); 
					}
				}
			}
			catch (Object& objError)
			{
				return;
			}
			catch(std::exception& e)
			{
				return;
			}
         }
        
        // qLowerBound() and qUpperBound() require our cachedCertTable list to be sorted in asc order
        qSort(cachedCertTable.begin(), cachedCertTable.end(), CertTableEntryLessThan());
    }

    void updateEntry(const QString &cert, const QString &title, const QString &data, const QString &expires_on,const QString &expires_in, const QString &expired, CertModelType type, int status)
    {
		if(!parent || parent->modelType != type)
		{
			return;
		}
        // Find cert / value in model
        QList<CertTableEntry>::iterator lower = qLowerBound(
            cachedCertTable.begin(), cachedCertTable.end(), cert, CertTableEntryLessThan());
        QList<CertTableEntry>::iterator upper = qUpperBound(
            cachedCertTable.begin(), cachedCertTable.end(), cert, CertTableEntryLessThan());
        int lowerIndex = (lower - cachedCertTable.begin());
        int upperIndex = (upper - cachedCertTable.begin());
        bool inModel = (lower != upper);
        CertTableEntry::Type newEntryType = CertTableEntry::Cert;

        switch(status)
        {
        case CT_NEW:
            if(inModel)
            {
                OutputDebugStringF("Warning: CertTablePriv::updateEntry: Got CT_NOW, but entry is already in model\n");
                break;
            }
            parent->beginInsertRows(QModelIndex(), lowerIndex, lowerIndex);
            cachedCertTable.insert(lowerIndex, CertTableEntry(newEntryType, title, cert, data, expires_on, expires_in, expired));
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
			lower->data = data;
			lower->expires_on = expires_on;
			lower->expires_in = expires_in;
			lower->expired = expired;
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

CertTableModel::CertTableModel(CWallet *wallet, WalletModel *parent,  CertModelType type) :
    QAbstractTableModel(parent),walletModel(parent),wallet(wallet),priv(0), modelType(type)
{
    columns << tr("Cert") << tr("Title") << tr("Data") << tr("Expires On") << tr("Expires In") << tr("Certificate Status");
    priv = new CertTablePriv(wallet, this);
    refreshCertTable();
}

CertTableModel::~CertTableModel()
{
    delete priv;
}
void CertTableModel::refreshCertTable() 
{
	if(modelType != MyCert)
		return;
	clear();
	priv->refreshCertTable(modelType);
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
        case Data:
            return rec->data;
        case Name:
            return rec->cert;
        case ExpiresOn:
            return rec->expires_on;
        case ExpiresIn:
            return rec->expires_in;
        case Expired:
            return rec->expired;
        }
    }
    else if (role == Qt::FontRole)
    {
        QFont font;
        if(index.column() == Name)
        {
            font = GUIUtil::bitcoinAddressFont();
        }
        return font;
    }
    else if (role == NameRole)
    {
        return rec->cert;
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
        case ExpiresOn:
            // Do nothing, if old value == new value
            if(rec->expires_on == value.toString())
            {
                editStatus = NO_CHANGES;
                return false;
            }
           
            break;
        case ExpiresIn:
            // Do nothing, if old value == new value
            if(rec->expires_in == value.toString())
            {
                editStatus = NO_CHANGES;
                return false;
            }
           
            break;
        case Expired:
            // Do nothing, if old value == new value
            if(rec->expired == value.toString())
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
       case Data:
            // Do nothing, if old value == new value
            if(rec->data == value.toString())
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
            // Check for duplicate certs to prevent accidental deletion of certs, if you try
            // to paste an existing cert over another cert (with a different label)
            else if(lookupCert(rec->cert) != -1)
            {
                editStatus = DUPLICATE_CERT;
                return false;
            }
            // Double-check that we're not overwriting a receiving cert
            else if(rec->type == CertTableEntry::Cert)
            {
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
    Qt::ItemFlags retval = Qt::ItemIsSelectable | Qt::ItemIsEnabled;
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

void CertTableModel::updateEntry(const QString &cert, const QString &value, const QString &data, const QString &expires_on,const QString &expires_in, const QString &expired, CertModelType type, int status)
{
    // Update cert book model from Syscoin core
    priv->updateEntry(cert, value, data, expires_on, expires_in, expired, type, status);
}

QString CertTableModel::addRow(const QString &type, const QString &cert, const QString &value, const QString &data, const QString &expires_on,const QString &expires_in, const QString &expired)
{
    std::string strCert = cert.toStdString();
    editStatus = OK;
    // Check for duplicate cert
    {
        LOCK(wallet->cs_wallet);
        if(lookupCert(cert) != -1)
        {
            editStatus = DUPLICATE_CERT;
            return QString();
        }
    }

    // Add entry

    return QString::fromStdString(strCert);
}
void CertTableModel::clear()
{
	beginResetModel();
    priv->cachedCertTable.clear();
	endResetModel();
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
