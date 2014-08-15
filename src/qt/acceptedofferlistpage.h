#ifndef ACCEPTEDOFFERLISTPAGE_H
#define ACCEPTEDOFFERLISTPAGE_H

#include <QDialog>

namespace Ui {
    class AcceptedOfferListPage;
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
class AcceptedOfferListPage : public QDialog
{
    Q_OBJECT

public:

    explicit AcceptedOfferListPage(QWidget *parent = 0);
    ~AcceptedOfferListPage();

    void setModel(OfferTableModel *model);
    void setOptionsModel(OptionsModel *optionsModel);
    const QString &getReturnValue() const { return returnValue; }
	bool handleURI(const QString &uri);
private:
    Ui::AcceptedOfferListPage *ui;
    OfferTableModel *model;
    OptionsModel *optionsModel;
    QString returnValue;
    QSortFilterProxyModel *proxyModel;
    QMenu *contextMenu;
    QAction *deleteAction; // to be able to explicitly disable it
    QString newOfferToSelect;

private slots:
    /** Copy offer of currently selected offer entry to clipboard */
    void on_copyOffer_clicked();
    /** Copy value of currently selected offer entry to clipboard (no button) */
    void onCopyOfferValueAction();

    /** Export button clicked */
    void on_exportButton_clicked();

    /** Set button states based on selected tab and selection */
    void selectionChanged();
    /** Spawn contextual menu (right mouse menu) for offer book entry */
    void contextualMenu(const QPoint &point);

};

#endif // ACCEPTEDOFFERLISTPAGE_H