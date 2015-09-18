#include "offeraccepttablemodel.h"

#include "guiutil.h"
#include "walletmodel.h"

#include "wallet.h"
#include "base58.h"

#include <QFont>
using namespace std;
using namespace json_spirit;

const QString OfferAcceptTableModel::Offer = "O";


extern const CRPCTable tableRPC;
struct OfferAcceptTableEntry
{
    enum Type {
        Offer
    };

    Type type;
    QString txid;
    QString offer;
	QString height;
	QString qty;
	QString currency;
	QString price;
	QString total;
	QString status;

    OfferAcceptTableEntry() {}
    OfferAcceptTableEntry(Type type, const QString &txid, const QString &offer, const QString &height,const QString &price, const QString &currency,const QString &qty,const QString &total, const QString &status):
        type(type), txid(txid), offer(offer), height(height),price(price), currency(currency),qty(qty), total(total), status(status) {}
};

struct OfferAcceptTableEntryLessThan
{
    bool operator()(const OfferAcceptTableEntry &a, const OfferAcceptTableEntry &b) const
    {
        return a.offer < b.offer;
    }
    bool operator()(const OfferAcceptTableEntry &a, const QString &b) const
    {
        return a.offer < b;
    }
    bool operator()(const QString &a, const OfferAcceptTableEntry &b) const
    {
        return a < b.offer;
    }
};

#define NAMEMAPTYPE map<vector<unsigned char>, uint256>

// Private implementation
class OfferAcceptTablePriv
{
public:
    CWallet *wallet;
    QList<OfferAcceptTableEntry> cachedOfferTable;
    OfferAcceptTableModel *parent;

    OfferAcceptTablePriv(CWallet *wallet, OfferAcceptTableModel *parent):
        wallet(wallet), parent(parent) {}

    void refreshOfferTable(OfferAcceptModelType type)
    {
        cachedOfferTable.clear();
        {
			string strMethod = string("offeracceptlist");
	        Array params; 
			Value result ;
			string name_str;
			string value_str;
			string height_str;
			string qty_str;
			string currency_str;
			string price_str;
			string total_str;
			string status_str;
			string ismine_str;
			

			try {
				result = tableRPC.execute(strMethod, params);		
				if (result.type() == array_type)
				{
					name_str = "";
					value_str = "";

			
			
					Array arr = result.get_array();
					BOOST_FOREACH(Value& input, arr)
					{
						if (input.type() != obj_type)
							continue;
						Object& o = input.get_obj();
						name_str = "";
						value_str = "";

					

				
						const Value& name_value = find_value(o, "offer");
						if (name_value.type() == str_type)
							name_str = name_value.get_str();
						const Value& value_value = find_value(o, "txid");
						if (value_value.type() == str_type)
							value_str = value_value.get_str();
						const Value& height_value = find_value(o, "height");
						if (height_value.type() == str_type)
							height_str = height_value.get_str();
						const Value& price_value = find_value(o, "price");
						if (price_value.type() == str_type)
							price_str = price_value.get_str();
						const Value& currency_value = find_value(o, "currency");
						if (currency_value.type() == str_type)
							currency_str = currency_value.get_str();
						const Value& qty_value = find_value(o, "quantity");
						if (qty_value.type() == str_type)
							qty_str = qty_value.get_str();
						const Value& total_value = find_value(o, "total");
						if (total_value.type() == str_type)
							total_str = total_value.get_str();
						const Value& status_value = find_value(o, "status");
						if (status_value.type() == str_type)
							status_str = status_value.get_str();
						const Value& ismine_value = find_value(o, "is_mine");
						if (ismine_value.type() == str_type)
							ismine_str = ismine_value.get_str();


						if((ismine_str == "false" && type == MyAccept) || (ismine_str == "true" && type == Accept))
							updateEntry(QString::fromStdString(name_str), QString::fromStdString(value_str), QString::fromStdString(height_str), QString::fromStdString(price_str), QString::fromStdString(currency_str), QString::fromStdString(qty_str), QString::fromStdString(total_str), QString::fromStdString(status_str),type, CT_NEW); 
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
        
        // qLowerBound() and qUpperBound() require our cachedOfferTable list to be sorted in asc order
        qSort(cachedOfferTable.begin(), cachedOfferTable.end(), OfferAcceptTableEntryLessThan());
    }

    void updateEntry(const QString &offer, const QString &txid, const QString &height,const QString &price, const QString &currency,const QString &qty,const QString &total, const QString &status, OfferAcceptModelType type, int statusi)
    {
		if(!parent || parent->modelType != type)
		{
			return;
		}
        // Find offer / value in model
        QList<OfferAcceptTableEntry>::iterator lower = qLowerBound(
            cachedOfferTable.begin(), cachedOfferTable.end(), txid, OfferAcceptTableEntryLessThan());
        QList<OfferAcceptTableEntry>::iterator upper = qUpperBound(
            cachedOfferTable.begin(), cachedOfferTable.end(), txid, OfferAcceptTableEntryLessThan());
        int lowerIndex = (lower - cachedOfferTable.begin());
        int upperIndex = (upper - cachedOfferTable.begin());
        bool inModel = (lower != upper);
        OfferAcceptTableEntry::Type newEntryType = OfferAcceptTableEntry::Offer;

        switch(statusi)
        {
        case CT_NEW:
            if(inModel)
            {
                OutputDebugStringF("Warning: OfferAcceptTablePriv::updateEntry: Got CT_NOW, but entry is already in model\n");
                break;
            }
            parent->beginInsertRows(QModelIndex(), lowerIndex, lowerIndex);
            cachedOfferTable.insert(lowerIndex, OfferAcceptTableEntry(newEntryType, txid, offer, height, price, currency, qty, total, status));
            parent->endInsertRows();
            break;
        case CT_UPDATED:
            if(!inModel)
            {
                OutputDebugStringF("Warning: OfferAcceptTablePriv::updateEntry: Got CT_UPDATED, but entry is not in model\n");
                break;
            }
            lower->type = newEntryType;
            lower->txid = txid;
			lower->height = height;
			lower->price = price;
			lower->currency = currency;
			lower->qty = qty;
			lower->total = total;
			lower->status = status;
            parent->emitDataChanged(lowerIndex);
            break;
        case CT_DELETED:
            if(!inModel)
            {
                OutputDebugStringF("Warning: OfferAcceptTablePriv::updateEntry: Got CT_DELETED, but entry is not in model\n");
                break;
            }
            parent->beginRemoveRows(QModelIndex(), lowerIndex, upperIndex-1);
            cachedOfferTable.erase(lower, upper);
            parent->endRemoveRows();
            break;
        }
    }

    int size()
    {
        return cachedOfferTable.size();
    }

    OfferAcceptTableEntry *index(int idx)
    {
        if(idx >= 0 && idx < cachedOfferTable.size())
        {
            return &cachedOfferTable[idx];
        }
        else
        {
            return 0;
        }
    }
};

OfferAcceptTableModel::OfferAcceptTableModel(CWallet *wallet, WalletModel *parent,  OfferAcceptModelType type) :
    QAbstractTableModel(parent),walletModel(parent),wallet(wallet),priv(0), modelType(type)
{
    columns << tr("Offer") << tr("TxID") << tr("Height") << tr("Price") << tr("Currency") << tr("Quantity") << tr("Total") << tr("Status");
    priv = new OfferAcceptTablePriv(wallet, this);
    refreshOfferTable();
}

OfferAcceptTableModel::~OfferAcceptTableModel()
{
    delete priv;
}
void OfferAcceptTableModel::refreshOfferTable() 
{
	clear();
	priv->refreshOfferTable(modelType);
}
int OfferAcceptTableModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return priv->size();
}

int OfferAcceptTableModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return columns.length();
}

QVariant OfferAcceptTableModel::data(const QModelIndex &index, int role) const
{
    if(!index.isValid())
        return QVariant();
    OfferAcceptTableEntry *rec = static_cast<OfferAcceptTableEntry*>(index.internalPointer());
    if(role == Qt::DisplayRole || role == Qt::EditRole)
    {
        switch(index.column())
        {
        case TxID:
            return rec->txid;
        case Name:
            return rec->offer;
        case Height:
            return rec->height;
        case Price:
            return rec->price;
        case Currency:
            return rec->currency;
        case Qty:
            return rec->qty;
        case Total:
            return rec->total;
        case Status:
            return rec->status;
        }
    }
    else if (role == TypeRole)
    {
        switch(rec->type)
        {
        case OfferAcceptTableEntry::Offer:
            return Offer;
        default: break;
        }
    }
    return QVariant();
}

bool OfferAcceptTableModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if(!index.isValid())
        return false;
    OfferAcceptTableEntry *rec = static_cast<OfferAcceptTableEntry*>(index.internalPointer());

    editStatus = OK;

    if(role == Qt::EditRole)
    {
        switch(index.column())
        {
        case Height:
            // Do nothing, if old value == new value
            if(rec->height == value.toString())
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
        case Currency:
            // Do nothing, if old value == new value
            if(rec->currency == value.toString())
            {
                editStatus = NO_CHANGES;
                return false;
            }
           
            break;
        case Qty:
            // Do nothing, if old value == new value
            if(rec->qty == value.toString())
            {
                editStatus = NO_CHANGES;
                return false;
            }
           
            break;
        case Total:
            // Do nothing, if old value == new value
            if(rec->total == value.toString())
            {
                editStatus = NO_CHANGES;
                return false;
            }
           
            break;
       case TxID:
            // Do nothing, if old value == new value
            if(rec->txid == value.toString())
            {
                editStatus = NO_CHANGES;
                return false;
            }
            // Check for duplicate offers to prevent accidental deletion of offers, if you try
            // to paste an existing offer over another offer (with a different label)
            else if(lookupOffer(rec->txid) != -1)
            {
                editStatus = DUPLICATE_OFFER;
                return false;
            }

            break;
        case Name:
            // Do nothing, if old offer == new offer
            if(rec->offer == value.toString())
            {
                editStatus = NO_CHANGES;
                return false;
            }

            break;
        }
        return true;
    }
    return false;
}

QVariant OfferAcceptTableModel::headerData(int section, Qt::Orientation orientation, int role) const
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

Qt::ItemFlags OfferAcceptTableModel::flags(const QModelIndex &index) const
{
    if(!index.isValid())
        return 0;
    Qt::ItemFlags retval = Qt::ItemIsSelectable | Qt::ItemIsEnabled;
    return retval;
}

QModelIndex OfferAcceptTableModel::index(int row, int column, const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    OfferAcceptTableEntry *data = priv->index(row);
    if(data)
    {
        return createIndex(row, column, priv->index(row));
    }
    else
    {
        return QModelIndex();
    }
}

void OfferAcceptTableModel::updateEntry(const QString &offer, const QString &value, const QString &height,const QString &price, const QString &currency, const QString &qty, const QString &total, const QString &status, OfferAcceptModelType type, int statusi)
{
    priv->updateEntry(offer, value, height, price, currency, qty, total, status, type, statusi);
}

QString OfferAcceptTableModel::addRow(const QString &type, const QString &offer, const QString &value, const QString &height,const QString &price, const QString &currency, const QString &qty, const QString &total, const QString &status)
{
    editStatus = OK;
    // Check for duplicate offer
    {
        LOCK(wallet->cs_wallet);
        if(lookupOffer(value) != -1)
        {
            editStatus = DUPLICATE_OFFER;
            return QString();
        }
    }

    // Add entry

    return value;
}
void OfferAcceptTableModel::clear()
{
	beginResetModel();
    priv->cachedOfferTable.clear();
	endResetModel();
}


int OfferAcceptTableModel::lookupOffer(const QString &value) const
{
    QModelIndexList lst = match(index(1, TxID, QModelIndex()),
                                Qt::EditRole, value, 1, Qt::MatchExactly);
    if(lst.isEmpty())
    {
        return -1;
    }
    else
    {
        return lst.at(0).row();
    }
}

void OfferAcceptTableModel::emitDataChanged(int idx)
{
    emit dataChanged(index(idx, 0, QModelIndex()), index(idx, columns.length()-1, QModelIndex()));
}
