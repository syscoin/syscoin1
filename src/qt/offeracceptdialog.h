#ifndef OFFERACCEPTDIALOG_H
#define OFFERACCEPTDIALOG_H

#include <QDialog>

namespace Ui {
    class OfferAcceptDialog;
}

class COffer;
/** Dialog for editing an address and associated information.
 */
class OfferAcceptDialog : public QDialog
{
    Q_OBJECT

public:
    explicit OfferAcceptDialog(QString title, QString price, QString quantity, QString offerAcceptGUID, QString notes, QWidget *parent=0);
    ~OfferAcceptDialog();

    bool getPaymentStatus();

private:
    Ui::OfferAcceptDialog *ui;
	QString quantity;
	QString title;
	QString price;
	QString notes;
	QString offerAcceptGUID;
	bool offerPaid; 

private slots:
	void on_cancelButton_clicked();
    void acceptOffer();
};

#endif // OFFERACCEPTDIALOG_H
