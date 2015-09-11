#include "offertablemodel.h"

#include "guiutil.h"
#include "walletmodel.h"

#include "wallet.h"
#include "base58.h"

#include <QFont>
using namespace std;
using namespace json_spirit;

const QString OfferTableModel::Offer = "O";


extern const CRPCTable tableRPC;
struct OfferTableEntry
{
    enum Type {
        Offer
    };

    Type type;
    QString title;
    QString offer;
	QString category;
	QString price;
	QString currency;
	QString qty;
	QString expired;

    OfferTableEntry() {}
    OfferTableEntry(Type type, const QString &title, const QString &offer, const QString &category,const QString &price, const QString &currency,const QString &qty,const QString &expired):
        type(type), title(title), offer(offer), category(category),price(price), currency(currency),qty(qty), expired(expired) {}
};

struct OfferTableEntryLessThan
{
    bool operator()(const OfferTableEntry &a, const OfferTableEntry &b) const
    {
        return a.offer < b.offer;
    }
    bool operator()(const OfferTableEntry &a, const QString &b) const
    {
        return a.offer < b;
    }
    bool operator()(const QString &a, const OfferTableEntry &b) const
    {
        return a < b.offer;
    }
};

#define NAMEMAPTYPE map<vector<unsigned char>, uint256>

// Private implementation
class OfferTablePriv
{
public:
    CWallet *wallet;
    QList<OfferTableEntry> cachedOfferTable;
    OfferTableModel *parent;

    OfferTablePriv(CWallet *wallet, OfferTableModel *parent):
        wallet(wallet), parent(parent) {}

    void refreshOfferTable(OfferModelType type)
    {
        cachedOfferTable.clear();
        {
			string strMethod = string("offerlist");
	        Array params; 
			Value result ;
			string name_str;
			string value_str;
			string category_str;
			string price_str;
			string currency_str;
			string qty_str;
			string expired_str;
			int expired = 0;

			

			try {
				result = tableRPC.execute(strMethod, params);
			}
			catch (Object& objError)
			{
				return;
			}
			catch(std::exception& e)
			{
				return;
			}
			if (result.type() == array_type)
			{
				name_str = "";
				value_str = "";

				expired = 0;


		
				Array arr = result.get_array();
				BOOST_FOREACH(Value& input, arr)
				{
					if (input.type() != obj_type)
						continue;
					Object& o = input.get_obj();
					name_str = "";
					value_str = "";

					expired = 0;


			
					const Value& name_value = find_value(o, "offer");
					if (name_value.type() == str_type)
						name_str = name_value.get_str();
					const Value& value_value = find_value(o, "title");
					if (value_value.type() == str_type)
						value_str = value_value.get_str();
					const Value& category_value = find_value(o, "category");
					if (category_value.type() == str_type)
						category_str = category_value.get_str();
					const Value& price_value = find_value(o, "price");
					if (price_value.type() == str_type)
						price_str = price_value.get_str();
					const Value& currency_value = find_value(o, "currency");
					if (currency_value.type() == str_type)
						currency_str = currency_value.get_str();
					const Value& qty_value = find_value(o, "quantity");
					if (qty_value.type() == str_type)
						qty_str = qty_value.get_str();
					const Value& expired_value = find_value(o, "expired");
					if (expired_value.type() == int_type)
						expired = expired_value.get_int();
					const Value& pending_value = find_value(o, "pending");
					int pending = 0;
					if (pending_value.type() == int_type)
						pending = pending_value.get_int();

					if(pending == 1)
					{
						expired_str = "Pending";
					}
					else if(expired == 1)
					{
						expired_str = "Expired";
					}
					else
					{
						expired_str = "Valid";
					}

					updateEntry(QString::fromStdString(name_str), QString::fromStdString(value_str), QString::fromStdString(category_str), QString::fromStdString(price_str), QString::fromStdString(currency_str), QString::fromStdString(qty_str), QString::fromStdString(expired_str),type, CT_NEW); 
				}
			}
            
         }
        
        // qLowerBound() and qUpperBound() require our cachedOfferTable list to be sorted in asc order
        qSort(cachedOfferTable.begin(), cachedOfferTable.end(), OfferTableEntryLessThan());
    }

    void updateEntry(const QString &offer, const QString &title, const QString &category,const QString &price, const QString &currency,const QString &qty,const QString &expired, OfferModelType type, int status)
    {
		if(!parent || parent->modelType != type)
		{
			return;
		}
        // Find offer / value in model
        QList<OfferTableEntry>::iterator lower = qLowerBound(
            cachedOfferTable.begin(), cachedOfferTable.end(), offer, OfferTableEntryLessThan());
        QList<OfferTableEntry>::iterator upper = qUpperBound(
            cachedOfferTable.begin(), cachedOfferTable.end(), offer, OfferTableEntryLessThan());
        int lowerIndex = (lower - cachedOfferTable.begin());
        int upperIndex = (upper - cachedOfferTable.begin());
        bool inModel = (lower != upper);
        OfferTableEntry::Type newEntryType = OfferTableEntry::Offer;

        switch(status)
        {
        case CT_NEW:
            if(inModel)
            {
                OutputDebugStringF("Warning: OfferTablePriv::updateEntry: Got CT_NOW, but entry is already in model\n");
                break;
            }
            parent->beginInsertRows(QModelIndex(), lowerIndex, lowerIndex);
            cachedOfferTable.insert(lowerIndex, OfferTableEntry(newEntryType, title, offer, category, price, currency, qty, expired));
            parent->endInsertRows();
            break;
        case CT_UPDATED:
            if(!inModel)
            {
                OutputDebugStringF("Warning: OfferTablePriv::updateEntry: Got CT_UPDATED, but entry is not in model\n");
                break;
            }
            lower->type = newEntryType;
            lower->title = title;
			lower->category = category;
			lower->price = price;
			lower->currency = currency;
			lower->qty = qty;
			lower->expired = expired;
            parent->emitDataChanged(lowerIndex);
            break;
        case CT_DELETED:
            if(!inModel)
            {
                OutputDebugStringF("Warning: OfferTablePriv::updateEntry: Got CT_DELETED, but entry is not in model\n");
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

    OfferTableEntry *index(int idx)
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

OfferTableModel::OfferTableModel(CWallet *wallet, WalletModel *parent,  OfferModelType type) :
    QAbstractTableModel(parent),walletModel(parent),wallet(wallet),priv(0), modelType(type)
{
    columns << tr("Offer") << tr("Title") << tr("Category") << tr("Price") << tr("Currency") << tr("Quantity") << tr("Status");
    priv = new OfferTablePriv(wallet, this);
    refreshOfferTable();
}

OfferTableModel::~OfferTableModel()
{
    delete priv;
}
void OfferTableModel::refreshOfferTable() 
{
	if(modelType != MyOffer)
		return;
	clear();
	priv->refreshOfferTable(modelType);
}
int OfferTableModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return priv->size();
}

int OfferTableModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return columns.length();
}

QVariant OfferTableModel::data(const QModelIndex &index, int role) const
{
    if(!index.isValid())
        return QVariant();

    OfferTableEntry *rec = static_cast<OfferTableEntry*>(index.internalPointer());

    if(role == Qt::DisplayRole || role == Qt::EditRole)
    {
        switch(index.column())
        {
        case Title:
            return rec->title;
        case Name:
            return rec->offer;
        case Category:
            return rec->category;
        case Price:
            return rec->price;
        case Currency:
            return rec->currency;
        case Qty:
            return rec->qty;
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
    else if (role == TypeRole)
    {
        switch(rec->type)
        {
        case OfferTableEntry::Offer:
            return Offer;
        default: break;
        }
    }
    return QVariant();
}

bool OfferTableModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if(!index.isValid())
        return false;
    OfferTableEntry *rec = static_cast<OfferTableEntry*>(index.internalPointer());

    editStatus = OK;

    if(role == Qt::EditRole)
    {
        switch(index.column())
        {
        case Category:
            // Do nothing, if old value == new value
            if(rec->category == value.toString())
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
        case Name:
            // Do nothing, if old offer == new offer
            if(rec->offer == value.toString())
            {
                editStatus = NO_CHANGES;
                return false;
            }
            // Check for duplicate offers to prevent accidental deletion of offers, if you try
            // to paste an existing offer over another offer (with a different label)
            else if(lookupOffer(rec->offer) != -1)
            {
                editStatus = DUPLICATE_OFFER;
                return false;
            }
            // Double-check that we're not overwriting a receiving offer
            else if(rec->type == OfferTableEntry::Offer)
            {
            }
            break;
        }
        return true;
    }
    return false;
}

QVariant OfferTableModel::headerData(int section, Qt::Orientation orientation, int role) const
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

Qt::ItemFlags OfferTableModel::flags(const QModelIndex &index) const
{
    if(!index.isValid())
        return 0;
    Qt::ItemFlags retval = Qt::ItemIsSelectable | Qt::ItemIsEnabled;
    return retval;
}

QModelIndex OfferTableModel::index(int row, int column, const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    OfferTableEntry *data = priv->index(row);
    if(data)
    {
        return createIndex(row, column, priv->index(row));
    }
    else
    {
        return QModelIndex();
    }
}

void OfferTableModel::updateEntry(const QString &offer, const QString &value, const QString &category,const QString &price, const QString &currency, const QString &qty, const QString &expired, OfferModelType type, int status)
{
    // Update alias book model from Syscoin core
    priv->updateEntry(offer, value, category, price, currency, qty, expired, type, status);
}

QString OfferTableModel::addRow(const QString &type, const QString &offer, const QString &value, const QString &category,const QString &price, const QString &currency, const QString &qty, const QString &expired)
{
    std::string strOffer = offer.toStdString();
    editStatus = OK;
    // Check for duplicate offer
    {
        LOCK(wallet->cs_wallet);
        if(lookupOffer(offer) != -1)
        {
            editStatus = DUPLICATE_OFFER;
            return QString();
        }
    }

    // Add entry

    return QString::fromStdString(strOffer);
}
void OfferTableModel::clear()
{
	beginResetModel();
    priv->cachedOfferTable.clear();
	endResetModel();
}


int OfferTableModel::lookupOffer(const QString &offer) const
{
    QModelIndexList lst = match(index(0, Name, QModelIndex()),
                                Qt::EditRole, offer, 1, Qt::MatchExactly);
    if(lst.isEmpty())
    {
        return -1;
    }
    else
    {
        return lst.at(0).row();
    }
}

void OfferTableModel::emitDataChanged(int idx)
{
    emit dataChanged(index(idx, 0, QModelIndex()), index(idx, columns.length()-1, QModelIndex()));
}
