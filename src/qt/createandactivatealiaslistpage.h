#ifndef CREATEANDACTIVATEALIASLISTPAGE_H
#define CREATEANDACTIVATEALIASLISTPAGE_H

#include <QDialog>

namespace Ui {
    class CreateandActivateAliasListPage;
}
class JSONRequest;
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
class CreateandActivateAliasListPage : public QDialog
{
    Q_OBJECT

public:


    explicit CreateandActivateAliasListPage(QWidget *parent = 0);
    ~CreateandActivateAliasListPage();

    void setModel(AliasTableModel *model);
    void setOptionsModel(OptionsModel *optionsModel);
    const QString &getReturnValue() const { return returnValue; }
	bool handleURI(const QString &uri);
public slots:
    void createandactivate();

private:
    Ui::CreateandActivateAliasListPage *ui;
    AliasTableModel *model;
    OptionsModel *optionsModel;
    QString returnValue;
    QSortFilterProxyModel *proxyModel;
    QMenu *contextMenu;
    QAction *deleteAction; // to be able to explicitly disable it


};

#endif // CREATEANDACTIVATEALIASLISTPAGE_H
