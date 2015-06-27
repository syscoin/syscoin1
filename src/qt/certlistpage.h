#ifndef CERTLISTPAGE_H
#define CERTLISTPAGE_H

#include <QDialog>

namespace Ui {
    class CertListPage;
}
class CertTableModel;
class OptionsModel;

QT_BEGIN_NAMESPACE
class QTableView;
class QItemSelection;
class QSortFilterProxyModel;
class QMenu;
class QModelIndex;
QT_END_NAMESPACE

/** Widget that shows a list of owned certes.
  */
class CertListPage : public QDialog
{
    Q_OBJECT

public:
    enum Tabs {
        CertTab = 0
    };

    enum Mode {
        ForTransferring, /**< Open cert book to pick cert for transferring */
        ForEditing  /**< Open cert book for editing */
    };

    explicit CertListPage(Mode mode, Tabs tab, QWidget *parent = 0);
    ~CertListPage();

    void setModel(CertTableModel *model);
    void setOptionsModel(OptionsModel *optionsModel);
    const QString &getReturnValue() const { return returnValue; }

public slots:
    void done(int retval);

private:
    Ui::CertListPage *ui;
    CertTableModel *model;
    OptionsModel *optionsModel;
    Mode mode;
    Tabs tab;
    QString returnValue;
    QSortFilterProxyModel *proxyModel;
    QMenu *contextMenu;
    QAction *deleteAction; // to be able to explicitly disable it
    QString newCertToSelect;

private slots:
    /** Create a new cert for receiving coins and / or add a new cert book entry */
    void on_newCert_clicked();
    /** Copy cert of currently selected cert entry to clipboard */
    void on_copyCert_clicked();

    /** Copy value of currently selected cert entry to clipboard (no button) */
    void onCopyCertValueAction();
    /** Edit currently selected cert entry (no button) */
    void onEditAction();
    /** Export button clicked */
    void on_exportButton_clicked();
    /** transfer the cert to a syscoin address  */
    void onTransferCertAction();

    /** Set button states based on selected tab and selection */
    void selectionChanged();
    /** Spawn contextual menu (right mouse menu) for cert book entry */
    void contextualMenu(const QPoint &point);
    /** New entry/entries were added to cert table */
    void selectNewCert(const QModelIndex &parent, int begin, int /*end*/);

signals:
    void transferCert(QString addr);
};

#endif // CERTLISTPAGE_H
