#ifndef EDITOFFERDIALOG_H
#define EDITOFFERDIALOG_H

#include <QDialog>

namespace Ui {
    class EditOfferDialog;
}
class OfferTableModel;
class WalletModel;
QT_BEGIN_NAMESPACE
class QDataWidgetMapper;
QT_END_NAMESPACE

/** Dialog for editing an offer
 */
class EditOfferDialog : public QDialog
{
    Q_OBJECT

public:
    enum Mode {
        NewOffer,
        EditOffer
    };

    explicit EditOfferDialog(Mode mode, QWidget *parent = 0);
    ~EditOfferDialog();

    void setModel(WalletModel*,OfferTableModel *model);
    void loadRow(int row);

    QString getOffer() const;
    void setOffer(const QString &offer);

public slots:
    void accept();

private:
    bool saveCurrentRow();

    Ui::EditOfferDialog *ui;
    QDataWidgetMapper *mapper;
    Mode mode;
    OfferTableModel *model;
	WalletModel* walletModel;
    QString offer;
};

#endif // EDITOFFERDIALOG_H
