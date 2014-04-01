#ifndef OFFERLISTPAGE_H
#define OFFERLISTPAGE_H

#include <QDialog>

namespace Ui {
    class OfferListPage;
}
class OfferTableModel;
class OptionsModel;

QT_BEGIN_NAMESPACE
class QTableView;
class QItemSelection;
class QSortFilterProxyModel;
class QMenu;
class QModelIndex;
QT_END_NAMESPACE

/** Widget that shows a list of owned offeres.
  */
class OfferListPage : public QDialog
{
    Q_OBJECT

public:
    enum Tabs {
        OfferTab = 0,
        OfferAcceptTab = 1
    };

    enum Mode {
        ForTransferring, /**< Open offer book to pick offer for transferring */
        ForEditing  /**< Open offer book for editing */
    };

    explicit OfferListPage(Mode mode, Tabs tab, QWidget *parent = 0);
    ~OfferListPage();

    void setModel(OfferTableModel *model);
    void setOptionsModel(OptionsModel *optionsModel);
    const QString &getReturnValue() const { return returnValue; }

public slots:
    void done(int retval);

private:
    Ui::OfferListPage *ui;
    OfferTableModel *model;
    OptionsModel *optionsModel;
    Mode mode;
    Tabs tab;
    QString returnValue;
    QSortFilterProxyModel *proxyModel;
    QMenu *contextMenu;
    QAction *deleteAction; // to be able to explicitly disable it
    QString newOfferToSelect;

private slots:
    /** Create a new offer for receiving coins and / or add a new offer book entry */
    void on_newOffer_clicked();
    /** Copy offer of currently selected offer entry to clipboard */
    void on_copyOffer_clicked();
    /** Open send coins dialog for currently selected offer (no button) */
    void on_transferOffer_clicked();
    /** Copy value of currently selected offer entry to clipboard (no button) */
    void onCopyOfferValueAction();
    /** Edit currently selected offer entry (no button) */
    void onEditAction();
    /** Export button clicked */
    void on_exportButton_clicked();
    /** transfer the offer to a syscoin address  */
    void onTransferOfferAction();

    /** Set button states based on selected tab and selection */
    void selectionChanged();
    /** Spawn contextual menu (right mouse menu) for offer book entry */
    void contextualMenu(const QPoint &point);
    /** New entry/entries were added to offer table */
    void selectNewOffer(const QModelIndex &parent, int begin, int /*end*/);

signals:
    void transferOffer(QString addr);
};

#endif // OFFERLISTPAGE_H
