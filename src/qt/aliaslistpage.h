#ifndef ALIASLISTPAGE_H
#define ALIASLISTPAGE_H

#include <QDialog>

namespace Ui {
    class AliasListPage;
}
class AliasTableModel;
class OptionsModel;

QT_BEGIN_NAMESPACE
class QTableView;
class QItemSelection;
class QSortFilterProxyModel;
class QMenu;
class QModelIndex;
QT_END_NAMESPACE

/** Widget that shows a list of owned aliases.
  */
class AliasListPage : public QDialog
{
    Q_OBJECT

public:
    enum Tabs {
        AliasTab = 0,
        DataAliasTab = 1
    };

    enum Mode {
        ForTransferring, /**< Open alias book to pick alias for transferring */
        ForEditing  /**< Open alias book for editing */
    };

    explicit AliasListPage(Mode mode, Tabs tab, QWidget *parent = 0);
    ~AliasListPage();

    void setModel(AliasTableModel *model);
    void setOptionsModel(OptionsModel *optionsModel);
    const QString &getReturnValue() const { return returnValue; }

public slots:
    void done(int retval);

private:
    Ui::AliasListPage *ui;
    AliasTableModel *model;
    OptionsModel *optionsModel;
    Mode mode;
    Tabs tab;
    QString returnValue;
    QSortFilterProxyModel *proxyModel;
    QMenu *contextMenu;
    QAction *deleteAction; // to be able to explicitly disable it
    QString newAliasToSelect;

private slots:
    /** Create a new alias for receiving coins and / or add a new alias book entry */
    void on_newAlias_clicked();
    /** Copy alias of currently selected alias entry to clipboard */
    void on_copyAlias_clicked();
    /** Open send coins dialog for currently selected alias (no button) */
    void on_transferAlias_clicked();
    /** Copy value of currently selected alias entry to clipboard (no button) */
    void onCopyAliasValueAction();
    /** Edit currently selected alias entry (no button) */
    void onEditAction();
    /** Export button clicked */
    void on_exportButton_clicked();
    /** transfer the alias to a syscoin address  */
    void onTransferAliasAction();

    /** Set button states based on selected tab and selection */
    void selectionChanged();
    /** Spawn contextual menu (right mouse menu) for alias book entry */
    void contextualMenu(const QPoint &point);
    /** New entry/entries were added to alias table */
    void selectNewAlias(const QModelIndex &parent, int begin, int /*end*/);

signals:
    void transferAlias(QString addr);
};

#endif // ALIASLISTPAGE_H
