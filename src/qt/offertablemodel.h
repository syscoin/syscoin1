#ifndef OFFERTABLEMODEL_H
#define OFFERTABLEMODEL_H

#include <QAbstractTableModel>
#include <QStringList>

class OfferTablePriv;
class CWallet;
class WalletModel;
enum OfferType {AllOffer=0,MyOffer};

typedef enum OfferType OfferModelType;

class OfferTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit OfferTableModel(CWallet *wallet, WalletModel *parent = 0, OfferModelType = AllOffer);
    ~OfferTableModel();

    enum ColumnIndex {
        Name = 0,   /**< offer name */
        Title = 1,  /**< Offer value */
		Description = 2,
		Category = 3,
		Price = 4,
		Currency = 5,
		Qty = 6,
		Expired = 7
    };

    enum RoleIndex {
        TypeRole = Qt::UserRole /**< Type of offer (#Send or #Receive) */
    };

    /** Return status of edit/insert operation */
    enum EditStatus {
        OK,                     /**< Everything ok */
        NO_CHANGES,             /**< No changes were made during edit operation */
        INVALID_OFFER,        /**< Unparseable offer */
        DUPLICATE_OFFER,      /**< Offer already in offer book */
        WALLET_UNLOCK_FAILURE  /**< Wallet could not be unlocked */
    };

    static const QString Offer;      /**< Specifies offerificate  */

    /** @name Methods overridden from QAbstractTableModel
        @{*/
    int rowCount(const QModelIndex &parent) const;
    int columnCount(const QModelIndex &parent) const;
    QVariant data(const QModelIndex &index, int role) const;
    bool setData(const QModelIndex &index, const QVariant &value, int role);
    QVariant headerData(int section, Qt::Orientation orientation, int role) const;
    QModelIndex index(int row, int column, const QModelIndex &parent) const;
    Qt::ItemFlags flags(const QModelIndex &index) const;
    /*@}*/

    /* Add an offer to the model.
       Returns the added offer on success, and an empty string otherwise.
     */
    QString addRow(const QString &type, const QString &offer, const QString &value, const QString &description, const QString &category,const QString &price, const QString &currency, const QString &qty, const QString &expired);

    /* Look up row index of an offer in the model.
       Return -1 if not found.
     */
    int lookupOffer(const QString &offer) const;
	void clear();
	void refreshOfferTable();
    EditStatus getEditStatus() const { return editStatus; }
	EditStatus editStatus;
private:
    WalletModel *walletModel;
    CWallet *wallet;
    OfferTablePriv *priv;
    QStringList columns;
	OfferModelType modelType;
    /** Notify listeners that data changed. */
    void emitDataChanged(int index);

public slots:
    /* Update offer list from core.
     */
    void updateEntry(const QString &offer, const QString &value, const QString &description, const QString &category, const QString &price, const QString &currency, const QString &qty, const QString &expired, OfferModelType type, int status);

    friend class OfferTablePriv;
};

#endif // OFFERTABLEMODEL_H
