#ifndef ACCEPTEDOFFERLISTPAGE_H
#define ACCEPTEDOFFERLISTPAGE_H

#include <QDialog>

namespace Ui {
    class AcceptedOfferListPage;
}
class OfferAcceptTableModel;
class OptionsModel;
class ClientModel;
class WalletModel;
QT_BEGIN_NAMESPACE
class QTableView;
class QItemSelection;
class QSortFilterProxyModel;
class QMenu;
class QModelIndex;
QT_END_NAMESPACE

/** Widget that shows a list of owned certes.
  */
class AcceptedOfferListPage : public QDialog
{
    Q_OBJECT

public:


    explicit AcceptedOfferListPage(QWidget *parent = 0);
    ~AcceptedOfferListPage();

    void setModel(WalletModel*, OfferAcceptTableModel *model);
    void setOptionsModel(ClientModel* clientmodel, OptionsModel *optionsModel);
    const QString &getReturnValue() const { return returnValue; }
	bool handleURI(const QString &uri);
	void showEvent ( QShowEvent * event );
public slots:
    void done(int retval);

private:
	ClientModel* clientModel;
	WalletModel *walletModel;
    Ui::AcceptedOfferListPage *ui;
    OfferAcceptTableModel *model;
    OptionsModel *optionsModel;
    QString returnValue;
    QSortFilterProxyModel *proxyModel;
    QMenu *contextMenu;
    QAction *deleteAction; // to be able to explicitly disable it
    QString newOfferToSelect;

private slots:

    /** Copy cert of currently selected cert entry to clipboard */
    void on_copyOffer_clicked();
    /** Copy value of currently selected cert entry to clipboard (no button) */
    void onCopyOfferValueAction();
    /** Export button clicked */
    void on_exportButton_clicked();
	void on_refreshButton_clicked();

    /** Set button states based on selected tab and selection */
    void selectionChanged();
    /** Spawn contextual menu (right mouse menu) for cert book entry */
    void contextualMenu(const QPoint &point);
    /** New entry/entries were added to cert table */
    void selectNewOffer(const QModelIndex &parent, int begin, int /*end*/);

};

#endif // ACCEPTEDOFFERLISTPAGE_H
